#ifndef _RTE_CUCKOO_HASH_H_                                                     
#define _RTE_CUCKOO_HASH_H_

#include "rte_jhash.h"
#include "rte_hash.h"

#define IPV6_ADDR_LEN 16
#define L3FWD_HASH_ENTRIES		(1024*1024*1)
/** Minimum Cache line size. */
#define RTE_CACHE_LINE_MIN_SIZE 64
#define RTE_CACHE_LINE_SIZE 64
#define IPPROTO_UDP 17

#define __rte_packed __attribute__((__packed__))
#define __rte_aligned(a) __attribute__((__aligned__(a)))
/** Force alignment to cache line. */
#define __rte_cache_aligned __rte_aligned(RTE_CACHE_LINE_SIZE)

#define RTE_MAX_ETHPORTS           64 //Not sure what the default val should be
#define RTE_ETHER_LOCAL_ADMIN_ADDR 0x02 /**< Locally assigned Eth. address. */
#define RETURN_IF_TRUE(cond, retval) do { \
	if (cond) \
		return retval; \
} while (0)
//typedef int32x4_t xmm_t;

#define RTE_HASH_BUCKET_ENTRIES		8
#define NULL_SIGNATURE			0

#define EMPTY_SLOT			0

#define KEY_ALIGNMENT			16

#define LCORE_CACHE_SIZE		64

#define RTE_HASH_BFS_QUEUE_MAX_LEN       1000

#define RTE_XABORT_CUCKOO_PATH_INVALIDED 0x4

#define RTE_HASH_TSX_MAX_RETRY  10


struct rte_hash_key {
	union {
		uintptr_t idata;
		void* pdata;
	};
	/* Variable key size */
	char key[0];
};

struct rte_hash_bucket {
	uint16_t sig_current[RTE_HASH_BUCKET_ENTRIES];

	uint32_t key_idx[RTE_HASH_BUCKET_ENTRIES];

	uint8_t flag[RTE_HASH_BUCKET_ENTRIES];

	void* next;
} __rte_cache_aligned;


struct rte_hash {
    char name[RTE_HASH_NAMESIZE];   /**< Name of the hash. */
    uint32_t entries;               /**< Total table entries. */
    uint32_t num_buckets;           /**< Number of buckets in table. */

	uint32_t key_len __rte_cache_aligned;


};

//static inline uint32_t dummy_func_link_check();
uint32_t dummy_func_link_check();

#endif

