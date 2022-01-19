#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 


#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/queue.h>


#include "include/rte_common.h"
#include "ring/rte_ring_elem.h"
/*
#include <rte_memory.h>
#include <rte_log.h>
#include <rte_prefetch.h>
#include <rte_branch_prediction.h>
#include <rte_malloc.h>
#include <rte_eal.h>
#include <rte_eal_memconfig.h>
#include <rte_per_lcore.h>
#include <rte_errno.h>
#include <rte_string_fns.h>
#include <rte_cpuflags.h>
#include <rte_rwlock.h>
#include <rte_spinlock.h>
#include <rte_ring_elem.h>
#include <rte_compat.h>
#include <rte_vect.h>
#include <rte_tailq.h>
*/



#define FOR_EACH_BUCKET(CURRENT_BKT, START_BUCKET)                            \
	for (CURRENT_BKT = START_BUCKET;                                      \
		CURRENT_BKT != NULL;                                          \
		CURRENT_BKT = CURRENT_BKT->next)

static inline void rte_prefetch0(const volatile void* p)
{
	asm volatile ("pld [%0]" : : "r" (p));
}

static inline int
rte_hash_cmp_eq(const void* key1, const void* key2, const struct rte_hash* h) {
	size_t key1size = sizeof(key1);
	return memcmp(key1, key2, key1size);
}



static inline uint32_t
alloc_slot(const struct rte_hash* h, struct lcore_cache* cached_free_slots)
{
	unsigned int n_slots;
	uint32_t slot_id;

	if (rte_ring_sc_dequeue_elem(h->free_slots, &slot_id,
		sizeof(uint32_t)) != 0)
		return EMPTY_SLOT;

	return slot_id;
}
//static inline uint32_t
uint32_t
dummy_func_link_check() {
    printf("DUMMY PRINT FOR LINK CHECK\n");
    return 0;
}


static inline uint16_t
get_short_sig(const hash_sig_t hash)
{
    return hash >> 16;
}

static inline uint32_t
get_prim_bucket_index(const struct rte_hash* h, const hash_sig_t hash)
{
    return hash & h->bucket_bitmask;
}

static inline uint32_t
get_alt_bucket_index(const struct rte_hash* h,
    uint32_t cur_bkt_idx, uint16_t sig)
{
    return (cur_bkt_idx ^ sig) & h->bucket_bitmask;
}


/* Read write locks implemented using rte_rwlock */
static inline void
__hash_rw_writer_lock(const struct rte_hash* h)
{
	if (h->writer_takes_lock && h->hw_trans_mem_support)
		//rte_rwlock_write_lock_tm(h->readwrite_lock);
		rte_rwlock_write_lock(h->readwrite_lock);
	else if (h->writer_takes_lock)
		rte_rwlock_write_lock(h->readwrite_lock);
}

static inline void
__hash_rw_reader_lock(const struct rte_hash* h)
{
	if (h->readwrite_concur_support && h->hw_trans_mem_support)
		//rte_rwlock_read_lock_tm(h->readwrite_lock);
		rte_rwlock_read_lock(h->readwrite_lock);
	else if (h->readwrite_concur_support)
		rte_rwlock_read_lock(h->readwrite_lock);
}

static inline void
__hash_rw_writer_unlock(const struct rte_hash* h)
{
	if (h->writer_takes_lock && h->hw_trans_mem_support)
		//rte_rwlock_write_unlock_tm(h->readwrite_lock);
		rte_rwlock_write_unlock(h->readwrite_lock);
	else if (h->writer_takes_lock)
		rte_rwlock_write_unlock(h->readwrite_lock);
}

static inline void
__hash_rw_reader_unlock(const struct rte_hash* h)
{
	if (h->readwrite_concur_support && h->hw_trans_mem_support)
		//rte_rwlock_read_unlock_tm(h->readwrite_lock);
		rte_rwlock_read_unlock(h->readwrite_lock);
	else if (h->readwrite_concur_support)
		rte_rwlock_read_unlock(h->readwrite_lock);
}


static inline void
enqueue_slot_back(const struct rte_hash* h,
	struct lcore_cache* cached_free_slots,
	uint32_t slot_id)
{
	rte_ring_sp_enqueue_elem(h->free_slots, &slot_id,
		sizeof(uint32_t));
}

static inline int32_t
search_and_update(const struct rte_hash* h, void* data, const void* key,
	struct rte_hash_bucket* bkt, uint16_t sig)
{
	int i;
	struct rte_hash_key* k, * keys = h->key_store;

	for (i = 0; i < RTE_HASH_BUCKET_ENTRIES; i++) {
		if (bkt->sig_current[i] == sig) {
			k = (struct rte_hash_key*)((char*)keys +
				bkt->key_idx[i] * h->key_entry_size);
			if (rte_hash_cmp_eq(key, k->key, h) == 0) {
				/* The store to application data at *data
				 * should not leak after the store to pdata
				 * in the key store. i.e. pdata is the guard
				 * variable. Release the application data
				 * to the readers.
				 */
				__atomic_store_n(&k->pdata,
					data,
					__ATOMIC_RELEASE);
				/*
				 * Return index where key is stored,
				 * subtracting the first dummy index
				 */
				return bkt->key_idx[i] - 1;
			}
		}
	}
	return -1;
}


static inline int32_t
rte_hash_cuckoo_insert_mw(const struct rte_hash* h,
	struct rte_hash_bucket* prim_bkt,
	struct rte_hash_bucket* sec_bkt,
	const struct rte_hash_key* key, void* data,
	uint16_t sig, uint32_t new_idx,
	int32_t* ret_val)
{
	unsigned int i;
	struct rte_hash_bucket* cur_bkt;
	int32_t ret;

	__hash_rw_writer_lock(h);
	/* Check if key was inserted after last check but before this
	 * protected region in case of inserting duplicated keys.
	 */
	ret = search_and_update(h, data, key, prim_bkt, sig);
	if (ret != -1) {
		__hash_rw_writer_unlock(h);
		*ret_val = ret;
		return 1;
	}

	FOR_EACH_BUCKET(cur_bkt, sec_bkt) {
		ret = search_and_update(h, data, key, cur_bkt, sig);
		if (ret != -1) {
			__hash_rw_writer_unlock(h);
			*ret_val = ret;
			return 1;
		}
	}

	/* Insert new entry if there is room in the primary
	 * bucket.
	 */
	for (i = 0; i < RTE_HASH_BUCKET_ENTRIES; i++) {
		/* Check if slot is available */
		if (likely(prim_bkt->key_idx[i] == EMPTY_SLOT)) {
			prim_bkt->sig_current[i] = sig;
			/* Store to signature and key should not
			 * leak after the store to key_idx. i.e.
			 * key_idx is the guard variable for signature
			 * and key.
			 */
			__atomic_store_n(&prim_bkt->key_idx[i],
				new_idx,
				__ATOMIC_RELEASE);
			break;
		}
	}
	__hash_rw_writer_unlock(h);

	if (i != RTE_HASH_BUCKET_ENTRIES)
		return 0;

	/* no empty entry */
	return -1;
}


static inline int
rte_hash_cuckoo_move_insert_mw(const struct rte_hash* h,
	struct rte_hash_bucket* bkt,
	struct rte_hash_bucket* alt_bkt,
	const struct rte_hash_key* key, void* data,
	struct queue_node* leaf, uint32_t leaf_slot,
	uint16_t sig, uint32_t new_idx,
	int32_t* ret_val)
{
	uint32_t prev_alt_bkt_idx;
	struct rte_hash_bucket* cur_bkt;
	struct queue_node* prev_node, * curr_node = leaf;
	struct rte_hash_bucket* prev_bkt, * curr_bkt = leaf->bkt;
	uint32_t prev_slot, curr_slot = leaf_slot;
	int32_t ret;

	__hash_rw_writer_lock(h);

	/* In case empty slot was gone before entering protected region */
	if (curr_bkt->key_idx[curr_slot] != EMPTY_SLOT) {
		__hash_rw_writer_unlock(h);
		return -1;
	}

	/* Check if key was inserted after last check but before this
	 * protected region.
	 */
	ret = search_and_update(h, data, key, bkt, sig);
	if (ret != -1) {
		__hash_rw_writer_unlock(h);
		*ret_val = ret;
		return 1;
	}

	FOR_EACH_BUCKET(cur_bkt, alt_bkt) {
		ret = search_and_update(h, data, key, cur_bkt, sig);
		if (ret != -1) {
			__hash_rw_writer_unlock(h);
			*ret_val = ret;
			return 1;
		}
	}

	while (likely(curr_node->prev != NULL)) {
		prev_node = curr_node->prev;
		prev_bkt = prev_node->bkt;
		prev_slot = curr_node->prev_slot;

		prev_alt_bkt_idx = get_alt_bucket_index(h,
			prev_node->cur_bkt_idx,
			prev_bkt->sig_current[prev_slot]);

		if (unlikely(&h->buckets[prev_alt_bkt_idx]
			!= curr_bkt)) {
			/* revert it to empty, otherwise duplicated keys */
			__atomic_store_n(&curr_bkt->key_idx[curr_slot],
				EMPTY_SLOT,
				__ATOMIC_RELEASE);
			__hash_rw_writer_unlock(h);
			return -1;
		}

		if (h->readwrite_concur_lf_support) {
			/* Inform the previous move. The current move need
			 * not be informed now as the current bucket entry
			 * is present in both primary and secondary.
			 * Since there is one writer, load acquires on
			 * tbl_chng_cnt are not required.
			 */
			__atomic_store_n(h->tbl_chng_cnt,
				*h->tbl_chng_cnt + 1,
				__ATOMIC_RELEASE);
			/* The store to sig_current should not
			 * move above the store to tbl_chng_cnt.
			 */
			__atomic_thread_fence(__ATOMIC_RELEASE);
		}

		/* Need to swap current/alt sig to allow later
		 * Cuckoo insert to move elements back to its
		 * primary bucket if available
		 */
		curr_bkt->sig_current[curr_slot] =
			prev_bkt->sig_current[prev_slot];
		/* Release the updated bucket entry */
		__atomic_store_n(&curr_bkt->key_idx[curr_slot],
			prev_bkt->key_idx[prev_slot],
			__ATOMIC_RELEASE);

		curr_slot = prev_slot;
		curr_node = prev_node;
		curr_bkt = curr_node->bkt;
	}

	if (h->readwrite_concur_lf_support) {
		/* Inform the previous move. The current move need
		 * not be informed now as the current bucket entry
		 * is present in both primary and secondary.
		 * Since there is one writer, load acquires on
		 * tbl_chng_cnt are not required.
		 */
		__atomic_store_n(h->tbl_chng_cnt,
			*h->tbl_chng_cnt + 1,
			__ATOMIC_RELEASE);
		/* The store to sig_current should not
		 * move above the store to tbl_chng_cnt.
		 */
		__atomic_thread_fence(__ATOMIC_RELEASE);
	}

	curr_bkt->sig_current[curr_slot] = sig;
	/* Release the new bucket entry */
	__atomic_store_n(&curr_bkt->key_idx[curr_slot],
		new_idx,
		__ATOMIC_RELEASE);

	__hash_rw_writer_unlock(h);

	return 0;

}

static inline int
rte_hash_cuckoo_make_space_mw(const struct rte_hash* h,
	struct rte_hash_bucket* bkt,
	struct rte_hash_bucket* sec_bkt,
	const struct rte_hash_key* key, void* data,
	uint16_t sig, uint32_t bucket_idx,
	uint32_t new_idx, int32_t* ret_val)
{
	unsigned int i;
	struct queue_node queue[RTE_HASH_BFS_QUEUE_MAX_LEN];
	struct queue_node* tail, * head;
	struct rte_hash_bucket* curr_bkt, * alt_bkt;
	uint32_t cur_idx, alt_idx;

	tail = queue;
	head = queue + 1;
	tail->bkt = bkt;
	tail->prev = NULL;
	tail->prev_slot = -1;
	tail->cur_bkt_idx = bucket_idx;

	/* Cuckoo bfs Search */
	while (likely(tail != head && head <
		queue + RTE_HASH_BFS_QUEUE_MAX_LEN -
		RTE_HASH_BUCKET_ENTRIES)) {
		curr_bkt = tail->bkt;
		cur_idx = tail->cur_bkt_idx;
		for (i = 0; i < RTE_HASH_BUCKET_ENTRIES; i++) {
			if (curr_bkt->key_idx[i] == EMPTY_SLOT) {
				int32_t ret = rte_hash_cuckoo_move_insert_mw(h,
					bkt, sec_bkt, key, data,
					tail, i, sig,
					new_idx, ret_val);
				if (likely(ret != -1))
					return ret;
			}

			/* Enqueue new node and keep prev node info */
			alt_idx = get_alt_bucket_index(h, cur_idx,
				curr_bkt->sig_current[i]);
			alt_bkt = &(h->buckets[alt_idx]);
			head->bkt = alt_bkt;
			head->cur_bkt_idx = alt_idx;
			head->prev = tail;
			head->prev_slot = i;
			head++;
		}
		tail++;
	}

	return -ENOSPC;
}




hash_sig_t
rte_hash_hash(const struct rte_hash* h, const void* key)
{
    /* calc hash result by key */
    return h->hash_func(key, h->key_len, h->hash_func_init_val);
}


static inline int32_t
__rte_hash_add_key_with_hash(const struct rte_hash* h, const void* key,
	hash_sig_t sig, void* data)
{
	uint16_t short_sig;
	uint32_t prim_bucket_idx, sec_bucket_idx;
	struct rte_hash_bucket* prim_bkt, * sec_bkt, * cur_bkt;
	struct rte_hash_key* new_k, * keys = h->key_store;
	uint32_t ext_bkt_id = 0;
	uint32_t slot_id;
	int ret;
	unsigned lcore_id;
	unsigned int i;
	struct lcore_cache* cached_free_slots = NULL;
	int32_t ret_val;
	struct rte_hash_bucket* last;

	short_sig = get_short_sig(sig);
	prim_bucket_idx = get_prim_bucket_index(h, sig);
	sec_bucket_idx = get_alt_bucket_index(h, prim_bucket_idx, short_sig);
	prim_bkt = &h->buckets[prim_bucket_idx];
	sec_bkt = &h->buckets[sec_bucket_idx];
	rte_prefetch0(prim_bkt);
	rte_prefetch0(sec_bkt);

	/* Check if key is already inserted in primary location */
	__hash_rw_writer_lock(h);
	ret = search_and_update(h, data, key, prim_bkt, short_sig);
	if (ret != -1) {
		__hash_rw_writer_unlock(h);
		return ret;
	}

	/* Check if key is already inserted in secondary location */
	FOR_EACH_BUCKET(cur_bkt, sec_bkt) {
		ret = search_and_update(h, data, key, cur_bkt, short_sig);
		if (ret != -1) {
			__hash_rw_writer_unlock(h);
			return ret;
		}
	}

	__hash_rw_writer_unlock(h);

	/* Did not find a match, so get a new slot for storing the new key */

	slot_id = alloc_slot(h, cached_free_slots);
	if (slot_id == EMPTY_SLOT) {
		printf("Alloc Slot failed!\n");
	}
	/*Don't understand DQ, so let's see if we can get by without it..*/
	/*
	if (slot_id == EMPTY_SLOT) {
		if (h->dq) {
			__hash_rw_writer_lock(h);
			ret = rte_rcu_qsbr_dq_reclaim(h->dq,
				h->hash_rcu_cfg->max_reclaim_size,
				NULL, NULL, NULL);
			__hash_rw_writer_unlock(h);
			if (ret == 0)
				slot_id = alloc_slot(h, cached_free_slots);
		}
		if (slot_id == EMPTY_SLOT)
			return -ENOSPC;
	}
	*/

	new_k = RTE_PTR_ADD(keys, slot_id * h->key_entry_size);
	/* The store to application data (by the application) at *data should
	 * not leak after the store of pdata in the key store. i.e. pdata is
	 * the guard variable. Release the application data to the readers.
	 */
	__atomic_store_n(&new_k->pdata,
		data,
		__ATOMIC_RELEASE);
	/* Copy key */
	memcpy(new_k->key, key, h->key_len);

	/* Find an empty slot and insert */
	ret = rte_hash_cuckoo_insert_mw(h, prim_bkt, sec_bkt, key, data,
		short_sig, slot_id, &ret_val);
	if (ret == 0)
		return slot_id - 1;
	else if (ret == 1) {
		enqueue_slot_back(h, cached_free_slots, slot_id);
		return ret_val;
	}

	/* Primary bucket full, need to make space for new entry */
	ret = rte_hash_cuckoo_make_space_mw(h, prim_bkt, sec_bkt, key, data,
		short_sig, prim_bucket_idx, slot_id, &ret_val);
	if (ret == 0)
		return slot_id - 1;
	else if (ret == 1) {
		enqueue_slot_back(h, cached_free_slots, slot_id);
		return ret_val;
	}

	/* Also search secondary bucket to get better occupancy */
	ret = rte_hash_cuckoo_make_space_mw(h, sec_bkt, prim_bkt, key, data,
		short_sig, sec_bucket_idx, slot_id, &ret_val);

	if (ret == 0)
		return slot_id - 1;
	else if (ret == 1) {
		enqueue_slot_back(h, cached_free_slots, slot_id);
		return ret_val;
	}

	/* if ext table not enabled, we failed the insertion */
	if (!h->ext_table_support) {
		enqueue_slot_back(h, cached_free_slots, slot_id);
		return ret;
	}

	/* Now we need to go through the extendable bucket. Protection is needed
	 * to protect all extendable bucket processes.
	 */
	__hash_rw_writer_lock(h);
	/* We check for duplicates again since could be inserted before the lock */
	ret = search_and_update(h, data, key, prim_bkt, short_sig);
	if (ret != -1) {
		enqueue_slot_back(h, cached_free_slots, slot_id);
		goto failure;
	}

	FOR_EACH_BUCKET(cur_bkt, sec_bkt) {
		ret = search_and_update(h, data, key, cur_bkt, short_sig);
		if (ret != -1) {
			enqueue_slot_back(h, cached_free_slots, slot_id);
			goto failure;
		}
	}

	/* Search sec and ext buckets to find an empty entry to insert. */
	FOR_EACH_BUCKET(cur_bkt, sec_bkt) {
		for (i = 0; i < RTE_HASH_BUCKET_ENTRIES; i++) {
			/* Check if slot is available */
			if (likely(cur_bkt->key_idx[i] == EMPTY_SLOT)) {
				cur_bkt->sig_current[i] = short_sig;
				/* Store to signature and key should not
				 * leak after the store to key_idx. i.e.
				 * key_idx is the guard variable for signature
				 * and key.
				 */
				__atomic_store_n(&cur_bkt->key_idx[i],
					slot_id,
					__ATOMIC_RELEASE);
				__hash_rw_writer_unlock(h);
				return slot_id - 1;
			}
		}
	}

	/* Failed to get an empty entry from extendable buckets. Link a new
	 * extendable bucket. We first get a free bucket from ring.
	 */
	if (rte_ring_sc_dequeue_elem(h->free_ext_bkts, &ext_bkt_id,
		sizeof(uint32_t)) != 0 ||
		ext_bkt_id == 0) {
		if (h->dq) {
			if (rte_rcu_qsbr_dq_reclaim(h->dq,
				h->hash_rcu_cfg->max_reclaim_size,
				NULL, NULL, NULL) == 0) {
				rte_ring_sc_dequeue_elem(h->free_ext_bkts,
					&ext_bkt_id,
					sizeof(uint32_t));
			}
		}
		if (ext_bkt_id == 0) {
			ret = -ENOSPC;
			goto failure;
		}
	}

	/* Use the first location of the new bucket */
	(h->buckets_ext[ext_bkt_id - 1]).sig_current[0] = short_sig;
	/* Store to signature and key should not leak after
	 * the store to key_idx. i.e. key_idx is the guard variable
	 * for signature and key.
	 */
	__atomic_store_n(&(h->buckets_ext[ext_bkt_id - 1]).key_idx[0],
		slot_id,
		__ATOMIC_RELEASE);
	/* Link the new bucket to sec bucket linked list */
	last = rte_hash_get_last_bkt(sec_bkt);
	last->next = &h->buckets_ext[ext_bkt_id - 1];
	__hash_rw_writer_unlock(h);
	return slot_id - 1;

failure:
	__hash_rw_writer_unlock(h);
	return ret;

}

int32_t
rte_hash_add_key(const struct rte_hash* h, const void* key)
{
    RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
    return __rte_hash_add_key_with_hash(h, key, rte_hash_hash(h, key), 0);
}