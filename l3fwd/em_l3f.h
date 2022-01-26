//#include "rte_hash.h"
#include <stdint.h>
#include "rte_hash.h"
#include "rte_cuckoo_hash.h"


#define NB_SOCKETS 8

struct rte_ipv6_hdr {
	uint32_t vtc_flow;	/**< IP version, traffic class & flow label. */
	uint16_t payload_len;	/**< IP payload size, including ext. headers */
	uint8_t  proto;		/**< Protocol, next header. */
	uint8_t  hop_limits;	/**< Hop limits. */
	uint8_t  src_addr[16];	/**< IP address of source host. */
	uint8_t  dst_addr[16];	/**< IP address of destination host(s). */
};

int em_dummy_print_func();

int multithread_check;

//struct rte_hash* ipv6_l3fwd_em_lookup_struct[NB_SOCKETS];
static inline uint32_t ipv6_hash_crc(const void* data, uint32_t data_len, uint32_t init_val);
struct rte_hash* setup_hash(int socket_id);
uint16_t em_get_ipv6_dst_port(void *ipv6_hdr, uint16_t portid, void *lookup_struct);
struct rte_ipv6_hdr get_ipv6_hdr(uint8_t port);