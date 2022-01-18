#include "em_l3f.h"
#include <stdio.h>
//#include "rte_hash.h"

int em_dummy_print_func(){
	printf("EM_DUMMY_PRINT\n");
	return 0;
}

int multithread_check;

static inline uint32_t
ipv6_hash_crc(const void* data, uint32_t init_val){

	return 0;
}