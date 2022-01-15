#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 
#include <stdio.h>

static inline uint32_t
get_prim_bucket_index(const struct rte_hash *h, const hash_sig_t hash)
{
    return hash;
    //return hash & h->bucket_bitmask;
}

static inline uint32_t
dummy_func_link_check() {
    printf("DUMMY PRINT FOR LINK CHECK\n");
    return 0;
}