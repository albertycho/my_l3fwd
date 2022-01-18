//#include "rte_hash.h"

#define NB_SOCKETS 8

int em_dummy_print_func();

struct rte_hash* ipv6_l3fwd_em_lookup_struct[NB_SOCKETS];
static inline uint32_t ipv6_hash_crc(const void* data, __rte_unused uint32_t data_len, uint32_t init_val);