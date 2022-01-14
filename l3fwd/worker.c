#include "util_from_hrd.h"
#include "main.h"
#include "mica.h"
#include "rpc.h"

#include <stdio.h>

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

static
bool herdCallbackFunction(uint8_t* slot_ptr, rpcArg_t* rpc_arguments)
{
    /* convert arguments passed by top-level thread */
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

void* run_worker(void* arg) {
    struct thread_params params = *(struct thread_params*)arg;
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(params.id + 3,&cpuset);
    int error = sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);
    if (error) {
        printf("Could not bind worker thread %d to core %d! (error %d)\n", params.id, params.id+1, error);
    } else {
        printf("Bound worker thread %d to core %d\n", params.id, params.id+3);
    }

    unsigned int wrkr_lid = params.id; /* Local ID of this worker thread*/

    /* MICA instance id = wrkr_lid, NUMA node = 0 */
    struct mica_kv kv;
    mica_init(&kv, wrkr_lid, 0, params.num_hash_buckets, params.log_capacity_bytes);
    mica_populate_fixed_len(&kv, params.num_keys, MICA_MAX_VALUE);

    rpcNUMAContext* rpcContext = params.ctx; /* Msutherl: Created in main.*/

    /* Msutherl: map local buffer and qp */
    uint64_t buf_size = get_lbuf_size(rpcContext);
    uint32_t* lbuf = NULL;
    NIExposedBuffer* myLocalBuffer = NULL;
#ifdef FLEXUS
    myLocalBuffer = registerNewLocalBuffer(rpcContext,&lbuf,buf_size,wrkr_lid);
    registerNewSONUMAQP(rpcContext,wrkr_lid);
#elif QFLEX
    pthread_mutex_lock(params.init_lock);
    myLocalBuffer = registerNewLocalBuffer(rpcContext,&lbuf,buf_size,wrkr_lid);
    registerNewSONUMAQP(rpcContext,wrkr_lid);
    pthread_mutex_unlock(params.init_lock);
#elif ZSIM
    myLocalBuffer = registerNewLocalBuffer(rpcContext,&lbuf,buf_size,wrkr_lid);
    //DLog("Local buffer at address %lld\n",myLocalBuffer);
    registerNewSONUMAQP(rpcContext,wrkr_lid);
#else // vm platform
    myLocalBuffer = trampolineToGetNIExposedBuffer(rpcContext,wrkr_lid);
#endif

    /* We can detect at most NUM_CLIENTS requests in each step */
    struct mica_resp resp_arr[NUM_CLIENTS];
    long long rolling_iter = 0; /* For throughput measurement */
    long long nb_tx_tot = 0;

    /* Setup the pointers to pass to the RPC callback, so it can access the data store */
    struct mica_pointers* datastore_pointer = (struct mica_pointers*) malloc(sizeof(struct mica_pointers*));
    datastore_pointer->kv = &kv;
    datastore_pointer->response_array = resp_arr;
    rpcArg_t args;
    args.pointerToAppData = datastore_pointer;

#if defined FLEXUS
    pthread_barrier_wait(params.barrier);
    if( wrkr_lid == 0) {
        fprintf(stdout,"Init done! Ready to start execution!\n");
        flexus_signal_all_set();
        ready_for_timing();
    }
#elif defined QFLEX
    ctx_disable_arm_timers(rpcContext);
    pthread_barrier_wait(params.barrier);
    if( wrkr_lid == 0) {
        fprintf(stdout,"Init done! Ready to start execution!\n");
        ctx_ready_timing(rpcContext);
    }
#elif defined ZSIM
    pthread_barrier_wait(params.barrier);
    //fprintf(stdout,"Init done! Ready to start execution!\n");
#endif

	bool * client_done;

#if defined ZSIM
	monitor_client_done(&client_done);
	//printf("monitor_client_done register done, returned addr:%lx\n");
#else
	bool dummy_true = true;
	client_done = &dummy_true;
#endif

	int tmp_count=0;
    //while ( 1 ) {
	
	//printf("HERD: before entering while loop\n");
    while (1) {
		//printf("HERD: after entering while loop\n");
        /* Begin new RPCValet */
        uint64_t source_node_id,source_qp_to_reply;
#if defined FLEXUS || defined QFLEX
        RPCWithHeader rpc = receiveRPCRequest_flex( rpcContext,
                (void*) datastore_pointer,
                params.sonuma_nid,
                wrkr_lid,
                &source_node_id,
                &source_qp_to_reply );

    	//printf("HERD: recievedRPCReq\n");

      /* the netpipe start/ends are only for direct on-cpu time, output by flexus stats
       * in the file core-occupancies.txt */
      bool is_get = herdCallbackFunction((uint8_t*) rpc.payload, &args );

    	//printf("HERD: after herdCallback\n");
      bool skip_ret_cpy = (resp_arr[0].val_len == 0);

      // resp_arr is already filled by the callback function, through the hacked
      // datastore pointer. This is ugly, would be nice to fix.
      sendToNode_flex( rpcContext, 
          myLocalBuffer, // where the response will come from
          (is_get && !skip_ret_cpy) ? resp_arr[0].val_len : 64, // sizeof is a full resp. for GET, CB for PUT
          source_node_id, // node id to reply to comes from the cq entry
          params.sonuma_nid,  // my nodeid
          source_qp_to_reply, // qp to reply to comes from the payload 
          wrkr_lid, // source qp
          true, // use true because response needs to go to a specific client
          (char*) resp_arr[0].val_ptr, // raw data
          skip_ret_cpy,
          nb_tx_tot
          ); 


      do_Recv_flex(rpcContext, params.sonuma_nid, wrkr_lid, 0, rpc.payload, 
          (is_get ? 64 : sizeof(struct mica_op))  // GETS only allocated 64B in reassembler. puts are a full op
          );
#elif defined ZSIM
	//notify_service_start(tmp_count);
    RPCWithHeader rpc = receiveRPCRequest_zsim( rpcContext,
                (void*) datastore_pointer,
                params.sonuma_nid,
                wrkr_lid,
                &source_node_id,
                &source_qp_to_reply,
                client_done );
		
        if((rpc.payload_len==0xdead))
            break;

	  tmp_count++;

        timestamp(tmp_count);

     // printf("HERD: after recvRPCReq\n");
	//notify_service_start(tmp_count);

      /* the netpipe start/ends are only for direct on-cpu time, output by flexus stats
       * in the file core-occupancies.txt */
      bool is_get = herdCallbackFunction((uint8_t*) rpc.payload, &args );

        timestamp(tmp_count);

      //printf("HERD: after herdCallback\n");
	  //printf("app: is_get = %s, serviced %d\n", is_get ? "true":"false", tmp_count);

      bool skip_ret_cpy = (resp_arr[0].val_len == 0);

      // resp_arr is already filled by the callback function, through the hacked
      // datastore pointer. This is ugly, would be nice to fix.
      sendToNode_zsim( rpcContext, 
          myLocalBuffer, // where the response will come from
          (is_get && !skip_ret_cpy) ? resp_arr[0].val_len : 64, // sizeof is a full resp. for GET, CB for PUT
          source_node_id, // node id to reply to comes from the cq entry
          params.sonuma_nid,  // my nodeid
          source_qp_to_reply, // qp to reply to comes from the payload 
          wrkr_lid, // source qp
          true, // use true because response needs to go to a specific client
          (char*) resp_arr[0].val_ptr, // raw data
          skip_ret_cpy,
          nb_tx_tot
          ); 

        timestamp(tmp_count);

      //printf("HERD: after sendtoNode\n");

      do_Recv_zsim(rpcContext, params.sonuma_nid, wrkr_lid, 0, rpc.payload, 
          (is_get ? 64 : sizeof(struct mica_op))  // GETS only allocated 64B in reassembler. puts are a full op
          );

        timestamp(tmp_count);

      //printf("HERD: after recvtoNode\n");
	//notify_service_end(tmp_count);
#else
        //// vm platform /////////
        receiveRPCRequest(  rpcContext,
                            &herdCallbackFunction,
                            (void*) datastore_pointer,
                            params.sonuma_nid,
                            wrkr_lid,
                            &source_node_id,
                            &source_qp_to_reply );

        // resp_arr is already filled by the callback function, through the hacked
        // datastore pointer. This is ugly, would be nice to fix.
        memcpy( trampolineToGetUnderlyingAddress(myLocalBuffer,0), (const void*) &(resp_arr[0]), sizeof(struct mica_resp) );
#ifdef PRINT_BUFFERS
        //printf("Server sending resp %d. Underlying buffer:\n", rolling_iter);
        DumpHex( trampolineToGetUnderlyingAddress(myLocalBuffer,0), sizeof(struct mica_resp) );
#endif

        sendToNode( rpcContext, 
                    myLocalBuffer, // where the response is written
                    sizeof(struct mica_resp), // sizeof
                    source_node_id, // the source node id is set by libsonuma
                    params.sonuma_nid,  // my nodeid
                    source_qp_to_reply , // targ. set by libsonuma
                    wrkr_lid, // source qp
                    true ); // use true because response needs to go to a specific client
#endif

        /* End new RPCValet */
        rolling_iter++;
        nb_tx_tot++;
		//notify_service_end(tmp_count);
    } // end infinite loop
    free(datastore_pointer);
	printf("HERD worker: requests serviced:%d\n", tmp_count);
	notify_done_to_zsim();

    return NULL;
}
 
