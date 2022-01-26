//#include "util_from_hrd.h"
#include "main.h"
//#include "mica.h"
#include "rpc.h"

#include <stdio.h>
#include "rte_jhash.h"
//#include "rte_cuckoo_hash.h"

#include "em_l3f.h"

//#include "zsim_nic_defines.hpp"

#define CPU_FREQ 2.0 // Flexus

#ifdef DEBUG_HERD

#define DHERD(M, ...) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__, ##__VA_ARGS__)
#define DHERD_NOARG(M) fprintf(stdout, "DEBUG %s:%d: " M "\n", __FILE__, __LINE__)
#else
#define DHERD(M, ...)
#define DHERD_NOARG(M)
#endif

static __inline__ unsigned long long rdtsc(void)
{
  unsigned long hi, lo;
  __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
  return ((unsigned long long)lo) | (((unsigned long long)hi)<<32) ;
}

static 
struct mica_op* convertRawToMicaOp(char* rawPtr)
{
    return ( (struct mica_op*) rawPtr );
}

/*
static
bool herdCallbackFunction(uint8_t* slot_ptr, rpcArg_t* rpc_arguments)
{
    //convert arguments passed by top-level thread
    struct mica_pointers* mptrs = (struct mica_pointers*) rpc_arguments->pointerToAppData;
    struct mica_kv* mica_store = mptrs->kv;
    struct mica_resp* resp_arr = mptrs->response_array;
        
    //unsigned long long rpc_start, rpc_end;
    //rpc_start = rdtsc();
    struct mica_op* validMICAOp = convertRawToMicaOp((char*)slot_ptr);
    // Convert to a MICA opcode
    validMICAOp->opcode -= HERD_MICA_OFFSET;
#ifdef DEBUG
    //printf("Received packet with opcode %d, len %d\n", validMICAOp->opcode, validMICAOp->val_len);
    assert(validMICAOp->opcode == MICA_OP_GET || validMICAOp->opcode == MICA_OP_PUT);
#endif
    mica_batch_op(mica_store, 1, &validMICAOp, resp_arr); // no batching

    return (validMICAOp->opcode == MICA_OP_GET);

    //rpc_end = rdtsc();
    //double serviceTime = ((double)rpc_end - rpc_start) / CPU_FREQ;
    //printf("time to execute this op: %lf ns\n", serviceTime);
    //addToContainer(rpcContext, serviceTime);
}
*/


static uint64_t l3fwd_em_handle_ipv6(RPCWithHeader rpc, uint32_t port_id, void* h, uint32_t ival /*ival is temp wa for sanity check testing*/){
    //get header from rpc
    struct rte_ipv6_hdr *ipv6_hdr;
    //ipv6_hdr = get_ipv6_hdr(rpc);
    uint64_t dst_port;

    uint8_t port = ival % 16;
    // // FIXME: temporary code for generating ipv6_hdr
    // struct ipv6_l3fwd_em_route entry;
    // union ipv6_5tuple_host newkey;

    // /* Create the ipv6 exact match flow */
    // memset(&entry, 0, sizeof(entry));
    // entry = ipv6_l3fwd_em_route_array[port];
    // entry.key.ip_dst[15] = (port + 1) % 256;//BYTE_VALUE_MAX;
    // convert_ipv6_5tuple(&entry.key, &newkey);

    printf("before calling get_ipv6_hdr\n");
    struct rte_ipv6_hdr ipv6hdr_var = get_ipv6_hdr(port);
    ipv6_hdr = &ipv6hdr_var;
    printf("before calling eM-get_ipv6_dst_port\n");
    printf("ipv6_hdr = %lx\n", ipv6_hdr);
    printf("payload_len: %d\n",ipv6_hdr->payload_len);
    dst_port = em_get_ipv6_dst_port(ipv6_hdr,port, h);
    return dst_port;

}

#define RTE_MAX_ETHPORTS 32

static uint64_t dest_eth_addr[RTE_MAX_ETHPORTS];
extern int multithread_check;

void* run_worker(void* arg) {
    struct thread_params params = *(struct thread_params*)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(params.id + params.start_core,&cpuset);
    int error = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (error) {
        printf("Could not bind worker thread %d to core %d! (error %d)\n", params.id, params.id+1, error);
    } else {
        printf("Bound worker thread %d to core %d\n", params.id, params.id+3);
    }
    dummy_func_link_check();
	em_dummy_print_func();
    int dummyint = rte_jhash_dummy_int();
    printf("dummyint = %d\n", dummyint);

    multithread_check = params.id;

    uint32_t socket_id = params.id % 16;
    struct rte_hash* worker_hash = setup_hash(socket_id);
    char name[RTE_HASH_NAMESIZE];
    printf("after setup hash\n");
    memcpy(name, worker_hash->name, sizeof(name));
    printf("after memcpy\n");
    printf("hash.name = %s\n",name);
    //printf("core %d setuphash complete. hash.name=%s\n", params.id, worker_hash->name);
    //printf("hash.freelost.name = %s\n", worker_hash->free_slots->name);

    uint32_t portid;
    /* pre-init dst MACs for all ports to 02:00:00:00:00:xx */
    for (portid = 0; portid < RTE_MAX_ETHPORTS; portid++) {
        dest_eth_addr[portid] =
            RTE_ETHER_LOCAL_ADMIN_ADDR + ((uint64_t)portid << 40);
    //    *(uint64_t*)(val_eth + portid) = dest_eth_addr[portid];
    }

    unsigned int wrkr_lid = params.id; /* Local ID of this worker thread*/

    /* MICA instance id = wrkr_lid, NUMA node = 0 */
    //struct mica_kv kv;
    //mica_init(&kv, wrkr_lid, 0, params.num_hash_buckets, params.log_capacity_bytes);
    //mica_populate_fixed_len(&kv, params.num_keys, MICA_MAX_VALUE);

    rpcNUMAContext* rpcContext = params.ctx; /* Msutherl: Created in main.*/

    /* Msutherl: map local buffer and qp */
    uint64_t buf_size = get_lbuf_size(rpcContext);
    uint32_t* lbuf = NULL;
    NIExposedBuffer* myLocalBuffer = NULL;
    myLocalBuffer = registerNewLocalBuffer(rpcContext,&lbuf,buf_size,wrkr_lid);
    //DLog("Local buffer at address %lld\n",myLocalBuffer);
    registerNewSONUMAQP(rpcContext,wrkr_lid);

#if defined ZSIM
    printf("sanity check for defined ZSIM\n");
#endif

	/* We can detect at most NUM_CLIENTS requests in each step */
    //struct mica_resp resp_arr[NUM_CLIENTS];
    long long rolling_iter = 0; /* For throughput measurement */
    long long nb_tx_tot = 0;

    /* Setup the pointers to pass to the RPC callback, so it can access the data store */
    //struct mica_pointers* datastore_pointer = (struct mica_pointers*) malloc(sizeof(struct mica_pointers*));
    //datastore_pointer->kv = &kv;
    //datastore_pointer->response_array = resp_arr;
    
    //rpcArg_t args;
    //args.pointerToAppData = datastore_pointer;

    pthread_barrier_wait(params.barrier);

	bool * client_done;

#if defined ZSIM
	monitor_client_done(&client_done);
	printf("monitor_client_done register done, returned addr:%lx\n");
#else
	bool dummy_true = true;
	client_done = &dummy_true;
#endif

	int tmp_count=0;
	
	printf("l3fwd: before entering while loop\n");
    while (1) {
		printf("l3fwd: after entering while loop, tmp_count=%d\n", tmp_count);
        /* Begin new RPCValet */
        uint64_t source_node_id,source_qp_to_reply;
	//notify_service_start(tmp_count);
        void* datastore_pointer=NULL;
        RPCWithHeader rpc = receiveRPCRequest_zsim_l3fwd( rpcContext,
                (void*) datastore_pointer,
                params.sonuma_nid,
                wrkr_lid,
                (uint16_t *)(&source_node_id),
                (uint16_t *)(&source_qp_to_reply),
                client_done );

        printf("l3fwd: 2, rpc.payload: %lx\n", rpc.payload);
       
		
        if((rpc.payload_len==0xdead))
            break;
//
	  tmp_count++;

        timestamp(tmp_count);

     // printf("HERD: after recvRPCReq\n");
	//notify_service_start(tmp_count);

      /* the netpipe start/ends are only for direct on-cpu time, output by flexus stats
       * in the file core-occupancies.txt */
      //bool is_get = herdCallbackFunction((uint8_t*) rpc.payload, &args );

        printf("l3fwd: 1\n");

        //TODO: write callback function for taking the packet and calling hash_lookup to find dst_port
        uint64_t dst_port;
        uint8_t port_id = tmp_count % 16;
        dst_port = l3fwd_em_handle_ipv6(rpc, port_id, (void*)worker_hash, port_id);
        printf("l3fwdloop: dst_port = %d\n", dst_port);

        //bool is_get = true; //put dummy for now, before implementing l3fwd callback

        timestamp(tmp_count);

      //printf("HERD: after herdCallback\n");
	  //printf("app: is_get = %s, serviced %d\n", is_get ? "true":"false", tmp_count);

      //bool skip_ret_cpy = (resp_arr[0].val_len == 0);
        bool skip_ret_cpy = true;
        char raw_data[1024];
      // resp_arr is already filled by the callback function, through the hacked
      // datastore pointer. This is ugly, would be nice to fix.
      sendToNode_zsim( rpcContext, 
          myLocalBuffer, // where the response will come from
          64, //(is_get && !skip_ret_cpy) ? resp_arr[0].val_len : 64, // sizeof is a full resp. for GET, CB for PUT
          source_node_id, // node id to reply to comes from the cq entry
          params.sonuma_nid,  // my nodeid
          source_qp_to_reply, // qp to reply to comes from the payload 
          wrkr_lid, // source qp
          true, // use true because response needs to go to a specific client
          //(char*) resp_arr[0].val_ptr, // raw data
          (char*) raw_data,
          skip_ret_cpy,
          nb_tx_tot
          ); 

        timestamp(tmp_count);
        printf("l3fwd: 2, rpc.payload: %lx\n", rpc.payload);
      //printf("HERD: after sendtoNode\n");

        do_Recv_zsim(rpcContext, params.sonuma_nid, wrkr_lid, 0, rpc.payload, 64);
          //(is_get ? 64 : sizeof(struct mica_op))  // GETS only allocated 64B in reassembler. puts are a full op
          //);
        printf("l3fwd: 3\n");
        timestamp(tmp_count);

      //printf("HERD: after recvtoNode\n");
	//notify_service_end(tmp_count);

		/* End new RPCValet */
        rolling_iter++;
        nb_tx_tot++;
		//notify_service_end(tmp_count);
    } // end infinite loop
    //free(datastore_pointer);
	printf("L3FWD worker: requests serviced:%d\n", tmp_count);

    printf("multithread check: %d\n", multithread_check );

	notify_done_to_zsim();

    return NULL;
}
 
