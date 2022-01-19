#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 


#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/queue.h>


#include "include/rte_common.h"
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



static inline void rte_prefetch0(const volatile void* p)
{
	asm volatile ("pld [%0]" : : "r" (p));
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
		rte_rwlock_write_lock_tm(h->readwrite_lock);
	else if (h->writer_takes_lock)
		rte_rwlock_write_lock(h->readwrite_lock);
}

static inline void
__hash_rw_reader_lock(const struct rte_hash* h)
{
	if (h->readwrite_concur_support && h->hw_trans_mem_support)
		rte_rwlock_read_lock_tm(h->readwrite_lock);
	else if (h->readwrite_concur_support)
		rte_rwlock_read_lock(h->readwrite_lock);
}

static inline void
__hash_rw_writer_unlock(const struct rte_hash* h)
{
	if (h->writer_takes_lock && h->hw_trans_mem_support)
		rte_rwlock_write_unlock_tm(h->readwrite_lock);
	else if (h->writer_takes_lock)
		rte_rwlock_write_unlock(h->readwrite_lock);
}

static inline void
__hash_rw_reader_unlock(const struct rte_hash* h)
{
	if (h->readwrite_concur_support && h->hw_trans_mem_support)
		rte_rwlock_read_unlock_tm(h->readwrite_lock);
	else if (h->readwrite_concur_support)
		rte_rwlock_read_unlock(h->readwrite_lock);
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
	if (h->use_local_cache) {
		lcore_id = rte_lcore_id();
		cached_free_slots = &h->local_free_slots[lcore_id];
	}
	slot_id = alloc_slot(h, cached_free_slots);
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

rte_hash_add_key(const struct rte_hash* h, const void* key)
{
    RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
    return __rte_hash_add_key_with_hash(h, key, rte_hash_hash(h, key), 0);
}