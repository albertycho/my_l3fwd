#ifndef _RTE_CUCKOO_HASH_H_                                                     
#define _RTE_CUCKOO_HASH_H_


struct rte_hash {
    char name[RTE_HASH_NAMESIZE];   /**< Name of the hash. */
    uint32_t entries;               /**< Total table entries. */
    uint32_t num_buckets;           /**< Number of buckets in table. */

};

#endif

