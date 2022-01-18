//#include "rte_hash.h"
#include <stdint.h>


#define NB_SOCKETS 8

int em_dummy_print_func();

struct rte_hash* ipv6_l3fwd_em_lookup_struct[NB_SOCKETS];
static inline uint32_t ipv6_hash_crc(const void* data, uint32_t init_val);