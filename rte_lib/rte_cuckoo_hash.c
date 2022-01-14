#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 

static inline uint32_t
get_prim_bucket_index(const struct rte_hash *h, const hash_sig_t hash)
{
    return hash;
    //return hash & h->bucket_bitmask;
}

