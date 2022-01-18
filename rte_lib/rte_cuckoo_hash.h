#ifndef _RTE_CUCKOO_HASH_H_                                                     
#define _RTE_CUCKOO_HASH_H_

#include "rte_jhash.h"
#include "rte_hash.h"

#define RTE_MAX_ETHPORTS           64 //Not sure what the default val should be
#define RTE_ETHER_LOCAL_ADMIN_ADDR 0x02 /**< Locally assigned Eth. address. */

struct rte_hash {
    char name[RTE_HASH_NAMESIZE];   /**< Name of the hash. */
    uint32_t entries;               /**< Total table entries. */
    uint32_t num_buckets;           /**< Number of buckets in table. */

};

//static inline uint32_t dummy_func_link_check();
uint32_t dummy_func_link_check();

#endif

