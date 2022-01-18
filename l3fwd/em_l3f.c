#include "em_l3f.h"
#include <stdio.h>
//#include "rte_hash.h"

#define IPV6_ADDR_LEN 16
#define L3FWD_HASH_ENTRIES		(1024*1024*1)
/** Minimum Cache line size. */
#define RTE_CACHE_LINE_MIN_SIZE 64
#define RTE_CACHE_LINE_SIZE 64

#define __rte_packed __attribute__((__packed__))
#define __rte_aligned(a) __attribute__((__aligned__(a)))
/** Force alignment to cache line. */
#define __rte_cache_aligned __rte_aligned(RTE_CACHE_LINE_SIZE)

int em_dummy_print_func(){
	printf("EM_DUMMY_PRINT\n");
	return 0;
}

int multithread_check;



struct ipv6_5tuple {
	uint8_t  ip_dst[IPV6_ADDR_LEN];
	uint8_t  ip_src[IPV6_ADDR_LEN];
	uint16_t port_dst;
	uint16_t port_src;
	uint8_t  proto;
} __rte_packed;

struct ipv6_l3fwd_em_route {
	struct ipv6_5tuple key;
	uint8_t if_out;
};
static const struct ipv6_l3fwd_em_route ipv6_l3fwd_em_route_array[] = {
	{{{32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 0},
	{{{32, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 1},
	{{{32, 1, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 2},
	{{{32, 1, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 3},
	{{{32, 1, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 4},
	{{{32, 1, 2, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 5},
	{{{32, 1, 2, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 6},
	{{{32, 1, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 7},
	{{{32, 1, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 8},
	{{{32, 1, 2, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 9, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 9},
	{{{32, 1, 2, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 10, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 10},
	{{{32, 1, 2, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 11, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 11},
	{{{32, 1, 2, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 12, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 12},
	{{{32, 1, 2, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 13, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 13},
	{{{32, 1, 2, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 14, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 14},
	{{{32, 1, 2, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 0},
	  {32, 1, 2, 0, 0, 0, 0, 15, 0, 0, 0, 0, 0, 0, 0, 1}, 9, 9, IPPROTO_UDP}, 15},
};

static uint8_t ipv6_l3fwd_out_if[L3FWD_HASH_ENTRIES] __rte_cache_aligned;

static inline uint32_t
ipv6_hash_crc(const void* data, uint32_t init_val){

	return 0;
}