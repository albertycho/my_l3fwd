#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 


#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
//#include <sys/queue.h>


#include "include/rte_common.h"
#include "ring/rte_ring_elem.h"
#include "include/rte_tailq.h"
#include "include/queue.h"

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

/* Mask of all flags supported by this version */
#define RTE_HASH_EXTRA_FLAGS_MASK (RTE_HASH_EXTRA_FLAGS_TRANS_MEM_SUPPORT | \
				   RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD | \
				   RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY | \
				   RTE_HASH_EXTRA_FLAGS_EXT_TABLE |	\
				   RTE_HASH_EXTRA_FLAGS_NO_FREE_ON_DEL | \
				   RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF)

#define FOR_EACH_BUCKET(CURRENT_BKT, START_BUCKET)                            \
	for (CURRENT_BKT = START_BUCKET;                                      \
		CURRENT_BKT != NULL;                                          \
		CURRENT_BKT = CURRENT_BKT->next)


TAILQ_HEAD(rte_hash_list, rte_tailq_entry);

static struct rte_tailq_elem rte_hash_tailq = {
	.name = "RTE_HASH",
};
//EAL_REGISTER_TAILQ(rte_hash_tailq)

//arm version - does not work
/*
static inline void rte_prefetch0(const volatile void* p)
{
	//compiler complaint.. maybe we can do without prefetching
	asm volatile ("pld [%0]" : : "r" (p));

}
*/
static inline void rte_prefetch0(const volatile void* p)
{
	asm volatile ("prefetcht0 %[p]" : : [p] "m" (*(const volatile char*)p));
}

static inline struct rte_hash_bucket*
rte_hash_get_last_bkt(struct rte_hash_bucket* lst_bkt)
{
	while (lst_bkt->next != NULL)
		lst_bkt = lst_bkt->next;
	return lst_bkt;
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
	//unsigned lcore_id;
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
		/*don't understand dq, see if we can get by without it..*/
		/*
		if (h->dq) {
			if (rte_rcu_qsbr_dq_reclaim(h->dq,
				h->hash_rcu_cfg->max_reclaim_size,
				NULL, NULL, NULL) == 0) {
				rte_ring_sc_dequeue_elem(h->free_ext_bkts,
					&ext_bkt_id,
					sizeof(uint32_t));
			}
		}
		*/
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


struct rte_hash*
	rte_hash_create(const struct rte_hash_parameters* params)
{
	struct rte_hash* h = NULL;
	struct rte_tailq_entry* te = NULL;
	struct rte_hash_list* hash_list;
	struct rte_ring* r = NULL;
	struct rte_ring* r_ext = NULL;
	char hash_name[RTE_HASH_NAMESIZE];
	void* k = NULL;
	void* buckets = NULL;
	void* buckets_ext = NULL;
	char ring_name[RTE_RING_NAMESIZE];
	char ext_ring_name[RTE_RING_NAMESIZE];
	unsigned num_key_slots;
	unsigned int hw_trans_mem_support = 0, use_local_cache = 0;
	unsigned int ext_table_support = 0;
	unsigned int readwrite_concur_support = 0;
	unsigned int writer_takes_lock = 0;
	unsigned int no_free_on_del = 0;
	uint32_t* ext_bkt_to_free = NULL;
	uint32_t* tbl_chng_cnt = NULL;
	struct lcore_cache* local_free_slots = NULL;
	unsigned int readwrite_concur_lf_support = 0;
	uint32_t i;

	rte_hash_function default_hash_func = (rte_hash_function)rte_jhash;

	hash_list = RTE_TAILQ_CAST(rte_hash_tailq.head, rte_hash_list);

	if (params == NULL) {
		//RTE_LOG(ERR, HASH, "rte_hash_create has no parameters\n");
		return NULL;
	}

	/* Check for valid parameters */
	if ((params->entries > RTE_HASH_ENTRIES_MAX) ||
		(params->entries < RTE_HASH_BUCKET_ENTRIES) ||
		(params->key_len == 0)) {
		//rte_errno = EINVAL;
		//RTE_LOG(ERR, HASH, "rte_hash_create has invalid parameters\n");
		return NULL;
	}

	if (params->extra_flag & ~RTE_HASH_EXTRA_FLAGS_MASK) {
		//rte_errno = EINVAL;
		//RTE_LOG(ERR, HASH, "rte_hash_create: unsupported extra flags\n");
		return NULL;
	}

	/* Validate correct usage of extra options */
	if ((params->extra_flag & RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY) &&
		(params->extra_flag & RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF)) {
		//rte_errno = EINVAL;
		//RTE_LOG(ERR, HASH, "rte_hash_create: choose rw concurrency or ""rw concurrency lock free\n");
		return NULL;
	}

	/* Check extra flags field to check extra options. */
	if (params->extra_flag & RTE_HASH_EXTRA_FLAGS_TRANS_MEM_SUPPORT)
		hw_trans_mem_support = 1;

	if (params->extra_flag & RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD) {
		use_local_cache = 1;
		writer_takes_lock = 1;
	}

	if (params->extra_flag & RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY) {
		readwrite_concur_support = 1;
		writer_takes_lock = 1;
	}

	if (params->extra_flag & RTE_HASH_EXTRA_FLAGS_EXT_TABLE)
		ext_table_support = 1;

	if (params->extra_flag & RTE_HASH_EXTRA_FLAGS_NO_FREE_ON_DEL)
		no_free_on_del = 1;

	if (params->extra_flag & RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF) {
		readwrite_concur_lf_support = 1;
		/* Enable not freeing internal memory/index on delete.
		 * If internal RCU is enabled, freeing of internal memory/index
		 * is done on delete
		 */
		no_free_on_del = 1;
	}

	/* Store all keys and leave the first entry as a dummy entry for lookup_bulk */

	//if (use_local_cache)
	//	/*
	//	 * Increase number of slots by total number of indices
	//	 * that can be stored in the lcore caches
	//	 * except for the first cache
	//	 */
	//	num_key_slots = params->entries + (RTE_MAX_LCORE - 1) *
	//	(LCORE_CACHE_SIZE - 1) + 1;
	//else
	//	num_key_slots = params->entries + 1;

	num_key_slots = params->entries + 1;

	snprintf(ring_name, sizeof(ring_name), "HT_%s", params->name);
	/* Create ring (Dummy slot index is not enqueued) */
	r = rte_ring_create_elem(ring_name, sizeof(uint32_t),
		rte_align32pow2(num_key_slots), params->socket_id, 0);
	if (r == NULL) {
		//RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err;
	}

	const uint32_t num_buckets = rte_align32pow2(params->entries) /
		RTE_HASH_BUCKET_ENTRIES;

	/* Create ring for extendable buckets. */
	if (ext_table_support) {
		snprintf(ext_ring_name, sizeof(ext_ring_name), "HT_EXT_%s",
			params->name);
		r_ext = rte_ring_create_elem(ext_ring_name, sizeof(uint32_t),
			rte_align32pow2(num_buckets + 1),
			params->socket_id, 0);

		if (r_ext == NULL) {
			//RTE_LOG(ERR, HASH, "ext buckets memory allocation " "failed\n");
			goto err;
		}
	}

	snprintf(hash_name, sizeof(hash_name), "HT_%s", params->name);

	//rte_mcfg_tailq_write_lock();
	rte_rwlock_write_lock(h->tailq_lock);

	/* guarantee there's no existing: this is normally already checked
	 * by ring creation above */
	TAILQ_FOREACH(te, (struct rte_hash_list *)hash_list, next) {
		h = (struct rte_hash*)te->data;
		if (strncmp(params->name, h->name, RTE_HASH_NAMESIZE) == 0)
			break;
	}
	h = NULL;
	if (te != NULL) {
		//rte_errno = EEXIST;
		te = NULL;
		goto err_unlock;
	}

	//te = rte_zmalloc("HASH_TAILQ_ENTRY", sizeof(*te), 0);
	te = malloc(sizeof(*te));
	if (te == NULL) {
		//RTE_LOG(ERR, HASH, "tailq entry allocation failed\n");
		goto err_unlock;
	}

	//h = (struct rte_hash*)rte_zmalloc_socket(hash_name, sizeof(struct rte_hash), RTE_CACHE_LINE_SIZE, params->socket_id);
	h = (struct rte_hash*)malloc(sizeof(struct rte_hash));

	if (h == NULL) {
		//RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err_unlock;
	}

	/*buckets = rte_zmalloc_socket(NULL,
		num_buckets * sizeof(struct rte_hash_bucket),
		RTE_CACHE_LINE_SIZE, params->socket_id);*/

	buckets = malloc(sizeof(struct rte_hash_bucket));

	if (buckets == NULL) {
		//RTE_LOG(ERR, HASH, "buckets memory allocation failed\n");
		goto err_unlock;
	}

	/* Allocate same number of extendable buckets */
	if (ext_table_support) {
		/*buckets_ext = rte_zmalloc_socket(NULL,
			num_buckets * sizeof(struct rte_hash_bucket),
			RTE_CACHE_LINE_SIZE, params->socket_id);*/
		bucket_ext = malloc(num_buckets * sizeof(struct rte_hash_bucekt));
		if (buckets_ext == NULL) {
			//RTE_LOG(ERR, HASH, "ext buckets memory allocation " "failed\n");
			goto err_unlock;
		}
		/* Populate ext bkt ring. We reserve 0 similar to the
		 * key-data slot, just in case in future we want to
		 * use bucket index for the linked list and 0 means NULL
		 * for next bucket
		 */
		for (i = 1; i <= num_buckets; i++)
			rte_ring_sp_enqueue_elem(r_ext, &i, sizeof(uint32_t));

		if (readwrite_concur_lf_support) {
			/*ext_bkt_to_free = rte_zmalloc(NULL, sizeof(uint32_t) *
				num_key_slots, 0);*/
			ext_bkt_to_free = malloc(sizeof(uint32_t) * num_key_slots);
			if (ext_bkt_to_free == NULL) {
				//RTE_LOG(ERR, HASH, "ext bkt to free memory allocation ""failed\n");
				goto err_unlock;
			}
		}
	}

	const uint32_t key_entry_size =
		RTE_ALIGN(sizeof(struct rte_hash_key) + params->key_len,
			KEY_ALIGNMENT);
	const uint64_t key_tbl_size = (uint64_t)key_entry_size * num_key_slots;

	/*k = rte_zmalloc_socket(NULL, key_tbl_size,
		RTE_CACHE_LINE_SIZE, params->socket_id);*/
	k = malloc(key_tbl_size);

	if (k == NULL) {
		//RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err_unlock;
	}

	//tbl_chng_cnt = rte_zmalloc_socket(NULL, sizeof(uint32_t), RTE_CACHE_LINE_SIZE, params->socket_id);
	tbl_chng_cnt = malloc(sizeof(uint32_t));

	if (tbl_chng_cnt == NULL) {
		//RTE_LOG(ERR, HASH, "memory allocation failed\n");
		goto err_unlock;
	}

	/*
	 * If x86 architecture is used, select appropriate compare function,
	 * which may use x86 intrinsics, otherwise use memcmp
	 */
#if defined(RTE_ARCH_X86) || defined(RTE_ARCH_ARM64)
	 /* Select function to compare keys */
	switch (params->key_len) {
	case 16:
		h->cmp_jump_table_idx = KEY_16_BYTES;
		break;
	case 32:
		h->cmp_jump_table_idx = KEY_32_BYTES;
		break;
	case 48:
		h->cmp_jump_table_idx = KEY_48_BYTES;
		break;
	case 64:
		h->cmp_jump_table_idx = KEY_64_BYTES;
		break;
	case 80:
		h->cmp_jump_table_idx = KEY_80_BYTES;
		break;
	case 96:
		h->cmp_jump_table_idx = KEY_96_BYTES;
		break;
	case 112:
		h->cmp_jump_table_idx = KEY_112_BYTES;
		break;
	case 128:
		h->cmp_jump_table_idx = KEY_128_BYTES;
		break;
	default:
		/* If key is not multiple of 16, use generic memcmp */
		h->cmp_jump_table_idx = KEY_OTHER_BYTES;
	}
#else
	h->cmp_jump_table_idx = KEY_OTHER_BYTES;
#endif

	//not using local cache

	//if (use_local_cache) {
	//	local_free_slots = rte_zmalloc_socket(NULL,
	//		sizeof(struct lcore_cache) * RTE_MAX_LCORE,
	//		RTE_CACHE_LINE_SIZE, params->socket_id);
	//	if (local_free_slots == NULL) {
	//		RTE_LOG(ERR, HASH, "local free slots memory allocation failed\n");
	//		goto err_unlock;
	//	}
	//}



	/* Default hash function */
#if defined(RTE_ARCH_X86)
	default_hash_func = (rte_hash_function)rte_hash_crc;
#elif defined(RTE_ARCH_ARM64)
	if (rte_cpu_get_flag_enabled(RTE_CPUFLAG_CRC32))
		default_hash_func = (rte_hash_function)rte_hash_crc;
#endif
	/* Setup hash context */
	strlcpy(h->name, params->name, sizeof(h->name));
	h->entries = params->entries;
	h->key_len = params->key_len;
	h->key_entry_size = key_entry_size;
	h->hash_func_init_val = params->hash_func_init_val;

	h->num_buckets = num_buckets;
	h->bucket_bitmask = h->num_buckets - 1;
	h->buckets = buckets;
	h->buckets_ext = buckets_ext;
	h->free_ext_bkts = r_ext;
	h->hash_func = (params->hash_func == NULL) ?
		default_hash_func : params->hash_func;
	h->key_store = k;
	h->free_slots = r;
	h->ext_bkt_to_free = ext_bkt_to_free;
	h->tbl_chng_cnt = tbl_chng_cnt;
	*h->tbl_chng_cnt = 0;
	h->hw_trans_mem_support = hw_trans_mem_support;
	h->use_local_cache = use_local_cache;
	h->local_free_slots = local_free_slots;
	h->readwrite_concur_support = readwrite_concur_support;
	h->ext_table_support = ext_table_support;
	h->writer_takes_lock = writer_takes_lock;
	h->no_free_on_del = no_free_on_del;
	h->readwrite_concur_lf_support = readwrite_concur_lf_support;

#if defined(RTE_ARCH_X86)
	if (rte_cpu_get_flag_enabled(RTE_CPUFLAG_SSE2))
		h->sig_cmp_fn = RTE_HASH_COMPARE_SSE;
	else
#elif defined(RTE_ARCH_ARM64)
	if (rte_cpu_get_flag_enabled(RTE_CPUFLAG_NEON))
		h->sig_cmp_fn = RTE_HASH_COMPARE_NEON;
	else
#endif
		h->sig_cmp_fn = RTE_HASH_COMPARE_SCALAR;

	/* Writer threads need to take the lock when:
	 * 1) RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY is enabled OR
	 * 2) RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD is enabled
	 */
	if (h->writer_takes_lock) {
		//h->readwrite_lock = rte_malloc(NULL, sizeof(rte_rwlock_t),RTE_CACHE_LINE_SIZE);
		h->readwrite_lock = malloc(sizeof(rte_rwlock_t));
		if (h->readwrite_lock == NULL)
			goto err_unlock;

		rte_rwlock_init(h->readwrite_lock);
	}
	//h->tailq_lock = rte_malloc(NULL, sizeof(rte_rwlock_t), RTE_CACHE_LINE_SIZE);
	h->tailq_lock = malloc(sizeof(rte_rwlock_t));
	if (h->tailq_lock == NULL) {
		goto err_unlock;
	}
	rte_rwlock_init(h->tailq_lock);

	/* Populate free slots ring. Entry zero is reserved for key misses. */
	for (i = 1; i < num_key_slots; i++)
		rte_ring_sp_enqueue_elem(r, &i, sizeof(uint32_t));

	te->data = (void*)h;
	TAILQ_INSERT_TAIL(hash_list, te, next);
	//rte_mcfg_tailq_write_unlock();
	rte_rwlock_write_unlock(h->tailq_lock);

	return h;
err_unlock:
	//rte_mcfg_tailq_write_unlock();
	rte_rwlock_write_unlock(h->tailq_lock);
err:
	rte_ring_free(r);
	rte_ring_free(r_ext);
	rte_free(te);
	rte_free(local_free_slots);
	rte_free(h);
	rte_free(buckets);
	rte_free(buckets_ext);
	rte_free(k);
	rte_free(tbl_chng_cnt);
	rte_free(ext_bkt_to_free);
	return NULL;
}