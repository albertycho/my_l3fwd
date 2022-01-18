#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 
#include <stdio.h>

static inline uint32_t
get_prim_bucket_index(const struct rte_hash *h, const hash_sig_t hash)
{
    return hash;
    //return hash & h->bucket_bitmask;
}

//static inline uint32_t
uint32_t
dummy_func_link_check() {
    printf("DUMMY PRINT FOR LINK CHECK\n");
    return 0;
}


hash_sig_t
rte_hash_hash(const struct rte_hash* h, const void* key)
{
    /* calc hash result by key */
    return h->hash_func(key, h->key_len, h->hash_func_init_val);
}


rte_hash_add_key(const struct rte_hash* h, const void* key)
{
    RETURN_IF_TRUE(((h == NULL) || (key == NULL)), -EINVAL);
    return __rte_hash_add_key_with_hash(h, key, rte_hash_hash(h, key), 0);
}