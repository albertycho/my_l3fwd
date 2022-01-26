#include "em_l3f.h"
#include <stdio.h>
//#include "rte_hash.h"
//#include "rte_cuckoo_hash.h"

/*
#define IPV6_ADDR_LEN 16
#define L3FWD_HASH_ENTRIES		(1024*1024*1)
// Minimum Cache line size. 
#define RTE_CACHE_LINE_MIN_SIZE 64
#define RTE_CACHE_LINE_SIZE 64
#define IPPROTO_UDP 17

#define __rte_packed __attribute__((__packed__))
#define __rte_aligned(a) __attribute__((__aligned__(a)))
//Force alignment to cache line. 
#define __rte_cache_aligned __rte_aligned(RTE_CACHE_LINE_SIZE)
*/
int em_dummy_print_func(){
	printf("EM_DUMMY_PRINT\n");
	return 0;
}

int multithread_check;


#define XMM_NUM_IN_IPV6_5TUPLE 3
struct ipv6_5tuple {
	uint8_t  ip_dst[IPV6_ADDR_LEN];
	uint8_t  ip_src[IPV6_ADDR_LEN];
	uint16_t port_dst;
	uint16_t port_src;
	uint8_t  proto;
} __rte_packed;

typedef __m128i xmm_t;
#define	XMM_SIZE	(sizeof(xmm_t))
#define	XMM_MASK	(XMM_SIZE - 1)

typedef union rte_xmm {
	xmm_t    x;
	uint8_t  u8[XMM_SIZE / sizeof(uint8_t)];
	uint16_t u16[XMM_SIZE / sizeof(uint16_t)];
	uint32_t u32[XMM_SIZE / sizeof(uint32_t)];
	uint64_t u64[XMM_SIZE / sizeof(uint64_t)];
	double   pd[XMM_SIZE / sizeof(double)];
} rte_xmm_t;

union ipv6_5tuple_host {
	struct {
		uint16_t pad0;
		uint8_t  proto;
		uint8_t  pad1;
		uint8_t  ip_src[IPV6_ADDR_LEN];
		uint8_t  ip_dst[IPV6_ADDR_LEN];
		uint16_t port_src;
		uint16_t port_dst;
		uint64_t reserve;
	};
	xmm_t xmm[XMM_NUM_IN_IPV6_5TUPLE];
};


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

#define ALL_32_BITS 0xffffffff
#define BIT_16_TO_23 0x00ff0000


static rte_xmm_t mask0;
static rte_xmm_t mask1;
static rte_xmm_t mask2;

void print128_num(__m128i var)
{
    uint16_t val[8];
    memcpy(val, &var, sizeof(val));
    printf("Numerical: %i %i %i %i %i %i %i %i \n", 
           val[0], val[1], val[2], val[3], val[4], val[5], 
           val[6], val[7]);
}

static inline uint32_t
ipv6_hash_crc(const void* data, uint32_t data_len, uint32_t init_val){
	//printf("ipv6_hash_crc called, data = %llx, init_val = %d\n", (uint32_t)data, init_val);
	//xmm_t printData[3];
	//memcpy(printData, &data, sizeof(union ipv6_5tuple_host));

	// //printf("data: %lx %lx %lx\n", printData[0], printData[1],printData[2];
	// print128_num(printData[0]);
	// print128_num(printData[1]);
	// print128_num(printData[2]);

	const union ipv6_5tuple_host *k;
	uint32_t t;
	const uint32_t *p;

	k = data;
	t = k->proto;
	p = (const uint32_t *)&k->port_src;

	init_val = rte_jhash_1word(t, init_val);
	init_val = rte_jhash(k->ip_src,
			sizeof(uint8_t) * IPV6_ADDR_LEN, init_val);
	init_val = rte_jhash(k->ip_dst,
			sizeof(uint8_t) * IPV6_ADDR_LEN, init_val);
	init_val = rte_jhash_1word(*p, init_val);
	return init_val;

	//return (uint32_t) data;
}


static void
convert_ipv6_5tuple(struct ipv6_5tuple* key1,
	union ipv6_5tuple_host* key2)
{
	printf("convert_ipv6_tuple: port_dst = %d, port_src = %d\n", key1->port_dst, key1->port_src);
	uint32_t i;

	for (i = 0; i < IPV6_ADDR_LEN; i++) {
		key2->ip_dst[i] = key1->ip_dst[i];
		key2->ip_src[i] = key1->ip_src[i];
	}
	key2->port_dst = (key1->port_dst);
	key2->port_src = (key1->port_src);
	key2->proto = key1->proto;
	key2->pad0 = 0;
	key2->pad1 = 0;
	key2->reserve = 0;
}

#define IPV6_L3FWD_EM_NUM_ROUTES RTE_DIM(ipv6_l3fwd_em_route_array)


static inline void
populate_ipv6_few_flow_into_table(const struct rte_hash* h)
{
	uint32_t i;
	int32_t ret;

	mask1 = (rte_xmm_t){ .u32 = {BIT_16_TO_23, ALL_32_BITS,
				ALL_32_BITS, ALL_32_BITS} };

	mask2 = (rte_xmm_t){ .u32 = {ALL_32_BITS, ALL_32_BITS, 0, 0} };

	for (i = 0; i < IPV6_L3FWD_EM_NUM_ROUTES; i++) {
		struct ipv6_l3fwd_em_route entry;
		union ipv6_5tuple_host newkey;

		entry = ipv6_l3fwd_em_route_array[i];
		convert_ipv6_5tuple(&entry.key, &newkey);
		//printf("i = %d, ");
		ret = rte_hash_add_key(h, (void*)&newkey);
		if (ret < 0) {
			//rte_exit(EXIT_FAILURE, "Unable to add entry %u to the l3fwd hash.\n", i);
			printf("Unable to add entry %u to the l3fwd hash.\n", i);
			exit(1);
		}
		printf("ret= %d, val(entry.if_out)= %d\n", ret, entry.if_out);
		ipv6_l3fwd_out_if[ret] = entry.if_out;
	}
	printf("Hash: Adding 0x%llx keys\n",
		(uint64_t)IPV6_L3FWD_EM_NUM_ROUTES);
}
static inline void
populate_ipv6_many_flow_into_table(const struct rte_hash *h,
		unsigned int nr_flow)
{
	unsigned i;

	mask1 = (rte_xmm_t){.u32 = {BIT_16_TO_23, ALL_32_BITS,
				ALL_32_BITS, ALL_32_BITS} };
	mask2 = (rte_xmm_t){.u32 = {ALL_32_BITS, ALL_32_BITS, 0, 0} };

	for (i = 0; i < nr_flow; i++) {
		uint8_t port = i % 16;//NUMBER_PORT_USED;
		struct ipv6_l3fwd_em_route entry;
		union ipv6_5tuple_host newkey;

		/* Create the ipv6 exact match flow */
		memset(&entry, 0, sizeof(entry));
		entry = ipv6_l3fwd_em_route_array[port];
		entry.key.ip_dst[15] = (port + 1) % 256;//BYTE_VALUE_MAX;
		convert_ipv6_5tuple(&entry.key, &newkey);
		int32_t ret = rte_hash_add_key(h, (void *) &newkey);

		if (ret < 0){
			//rte_exit(EXIT_FAILURE, "Unable to add entry %u\n", i);
			printf("Unable to add entry %u to the l3fwd hash.\n", i);
			exit(1);
		}
		printf("ret= %d, val(entry.if_out)= %d\n", ret, entry.if_out);
		ipv6_l3fwd_out_if[ret] = (uint8_t) entry.if_out;

	}
	printf("Hash: Adding 0x%x keys\n", nr_flow);
}


struct rte_hash* setup_hash(int socket_id){
//void setup_hash(int socket_id){

	struct rte_hash * ipv6_l3fwd_lookup=NULL;
	
	uint32_t hash_entry_number = 16; //16 is default, make it programmable?

	struct rte_hash_parameters ipv6_l3fwd_hash_params = {
		.name = NULL,
		.entries = L3FWD_HASH_ENTRIES,
		.key_len = sizeof(union ipv6_5tuple_host),
		.hash_func = ipv6_hash_crc,
		.hash_func_init_val = 0,
	};

	ipv6_l3fwd_hash_params.name = "ipv6_l3fwd_hash";
	ipv6_l3fwd_hash_params.socket_id = socket_id;

	//uint64_t hash_addr = rte_hash_create(&ipv6_l3fwd_hash_params);
	//////TODO: FIXME: There is a bug where returned pointer's address is 1 extended..
	//hash_addr = hash_addr & 0x7FFFFFFFFFFF;

	//ipv6_l3fwd_lookup = (struct rte_hash*)hash_addr;
	
	int hcret = rte_hash_create(&ipv6_l3fwd_hash_params, &ipv6_l3fwd_lookup);
	
	//ipv6_l3fwd_lookup = rte_hash_create(&ipv6_l3fwd_hash_params);
	if (hcret!=1) {
		printf("setup hash failed - rte_hash_create fail\n");
	}
	if(ipv6_l3fwd_lookup==NULL){
		printf("setup hash failed - rte_hash_create fail\n");
	}

	printf("ipv6_l3fwd_lookup's addr = %lx\n", (uint64_t)ipv6_l3fwd_lookup);

	print_hash_names(ipv6_l3fwd_lookup);
	printf("h->freeslot's addr = %lx\n", (uint64_t)(ipv6_l3fwd_lookup->free_slots));
	//printf("rte_hash_create returned in setup_hash\n");
	//printf("ipv6_l3fwd_lookup's addr = %lx\n", (uint64_t)ipv6_l3fwd_lookup);
	//printf("setup_hash: hash.name = %s\n",ipv6_l3fwd_lookup->name);
	//char* ringname = (char*)(ipv6_l3fwd_lookup->free_slots);
	//printf("ringname: %s\n",ringname);
	
	
	//import this function
	//populate_ipv6_many_flow_into_table(ipv6_l3fwd_lookup, hash_entry_number);
	//populate_ipv6_few_flow_into_table(ipv6_l3fwd_lookup);
	//populate_ipv6_many_flow_into_table(ipv6_l3fwd_lookup, 1024);
	populate_ipv6_many_flow_into_table(ipv6_l3fwd_lookup, 16);

	//dump_keys_buckets(ipv6_l3fwd_lookup);
	return ipv6_l3fwd_lookup;

}
static inline xmm_t
em_mask_key(void *key, xmm_t mask)
{
	__m128i data = _mm_loadu_si128((__m128i *)(key));

	return _mm_and_si128(data, mask);
}

//static inline uint16_t
uint16_t
em_get_ipv6_dst_port(void *ipv6_hdr, uint16_t portid, void *lookup_struct)
{
	int ret = 0;
	union ipv6_5tuple_host key;
	struct rte_hash *ipv6_l3fwd_lookup_struct =
		(struct rte_hash *)lookup_struct;

	ipv6_hdr = (uint8_t *)ipv6_hdr +
		offsetof(struct rte_ipv6_hdr, payload_len);
	void *data0 = ipv6_hdr;
	void *data1 = ((uint8_t *)ipv6_hdr) + sizeof(xmm_t);
	void *data2 = ((uint8_t *)ipv6_hdr) + sizeof(xmm_t) + sizeof(xmm_t);

	printf("before calling em_mask_key\n");

	/* Get part of 5 tuple: src IP address lower 96 bits and protocol */
	key.xmm[0] = em_mask_key(data0, mask1.x);

	/*
	 * Get part of 5 tuple: dst IP address lower 96 bits
	 * and src IP address higher 32 bits.
	 */

	printf("before dereferencing\n");
	printf("data1(addr) = %lx\n", data1);

	//key.xmm[1] = *(xmm_t *)data1;
	key.xmm[1] = _mm_loadu_si128(data1);
	
	printf("before calling em_mask_key 2\n");
	/*
	 * Get part of 5 tuple: dst port and src port
	 * and dst IP address higher 32 bits.
	 */
	key.xmm[2] = em_mask_key(data2, mask2.x);

	printf("before calling hash_lookup\n");
	/* Find destination port */
	//ret = rte_hash_lookup(ipv6_l3fwd_lookup_struct, (const void *)&key);

	//test code, TODO: remove
		struct ipv6_l3fwd_em_route entry;
		union ipv6_5tuple_host newkey;

		/* Create the ipv6 exact match flow */
		memset(&entry, 0, sizeof(entry));
		entry = ipv6_l3fwd_em_route_array[portid];
		entry.key.ip_dst[15] = (portid + 1) % 256;//BYTE_VALUE_MAX;
		convert_ipv6_5tuple(&entry.key, &newkey);
	ret = rte_hash_lookup(ipv6_l3fwd_lookup_struct, (const void *)&newkey);

	printf("hash_lookup returned %d\n", ret);
	return (ret < 0) ? portid : ipv6_l3fwd_out_if[ret];
}

struct rte_ipv6_hdr get_ipv6_hdr(uint8_t port){
	struct rte_ipv6_hdr new_hdr;
	memset(&new_hdr, 0, sizeof(new_hdr));

	struct ipv6_l3fwd_em_route entry;
	entry = ipv6_l3fwd_em_route_array[port];
	entry.key.ip_dst[15] = (port + 1) % 256;//BYTE_VALUE_MAX;

	struct ipv6_5tuple* key1 = &(entry.key);

	for (uint32_t i = 0; i < IPV6_ADDR_LEN; i++) {
		new_hdr.dst_addr[i] = key1->ip_dst[i];
		new_hdr.src_addr[i] = key1->ip_src[i];
	}
	new_hdr.proto = key1->proto;
	new_hdr.payload_len = 10;

	return new_hdr;

}