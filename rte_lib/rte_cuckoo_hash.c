#include "rte_hash.h"
#include "rte_cuckoo_hash.h" 


#include <string.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <sys/queue.h>

#include <rte_common.h>
#include <rte_memory.h>         /* for definition of RTE_CACHE_LINE_SIZE */
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