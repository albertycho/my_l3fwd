#include <stdint.h>                                                             
#include <stddef.h>    
#include <rte_jhash.h>

#ifndef _RTE_HASH_H_
#define _RTE_HASH_H_
                                                                               
/** Maximum size of hash table that can be created. */                          
#define RTE_HASH_ENTRIES_MAX            (1 << 30)                               
                                                                                
/** Maximum number of characters in hash name.*/                                
#define RTE_HASH_NAMESIZE           32                                          
                                                                                
/** Maximum number of keys that can be searched for using rte_hash_lookup_bulk. */
#define RTE_HASH_LOOKUP_BULK_MAX        64                                      
#define RTE_HASH_LOOKUP_MULTI_MAX       RTE_HASH_LOOKUP_BULK_MAX                
                                                                                
/** Enable Hardware transactional memory support. */                            
#define RTE_HASH_EXTRA_FLAGS_TRANS_MEM_SUPPORT  0x01                            
                                                                                
/** Default behavior of insertion, single writer/multi writer */                
#define RTE_HASH_EXTRA_FLAGS_MULTI_WRITER_ADD 0x02                              
                                                                                
/** Flag to support reader writer concurrency */                                
#define RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY 0x04                                
                                                                                
/** Flag to indicate the extendable bucket table feature should be used */      
#define RTE_HASH_EXTRA_FLAGS_EXT_TABLE 0x08                                     
                                                                                
/** Flag to disable freeing of key index on hash delete.                        
 * Refer to rte_hash_del_xxx APIs for more details.                             
 * This is enabled by default when RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF       
 * is enabled. However, if internal RCU is enabled, freeing of internal         
 * memory/index is done on delete                                               
 */                                                                             
#define RTE_HASH_EXTRA_FLAGS_NO_FREE_ON_DEL 0x10                                
                                                                                
/** Flag to support lock free reader writer concurrency. Both single writer     
 * and multi writer use cases are supported.                                    
 */                                                                             
#define RTE_HASH_EXTRA_FLAGS_RW_CONCURRENCY_LF 0x20                             
               

typedef uint32_t hash_sig_t;                                                    
                                                                                
/** Type of function that can be used for calculating the hash value. */        
typedef uint32_t (*rte_hash_function)(const void *key, uint32_t key_len,        
                      uint32_t init_val);                                       
                                                                                
/** Type of function used to compare the hash key. */                           
typedef int (*rte_hash_cmp_eq_t)(const void *key1, const void *key2, size_t key_len);
                                                                                
/**                                                                             
 * Type of function used to free data stored in the key.                        
 * Required when using internal RCU to allow application to free key-data once  
 * the key is returned to the ring of free key-slots.                           
 */                                                                             
typedef void (*rte_hash_free_key_data)(void *p, void *key_data);                
                                                                                
/**                                                                             
 * Parameters used when creating the hash table.                                
 */                                                                             
struct rte_hash_parameters {                                                    
    const char *name;       /**< Name of the hash. */                           
    uint32_t entries;       /**< Total hash table entries. */                   
    uint32_t reserved;      /**< Unused field. Should be set to 0 */            
    uint32_t key_len;       /**< Length of hash key. */                         
    rte_hash_function hash_func;    /**< Primary Hash function used to calculate hash. */
    uint32_t hash_func_init_val;    /**< Init value used by hash_func. */       
    int socket_id;          /**< NUMA Socket ID for memory. */                  
    uint8_t extra_flag;     /**< Indicate if additional parameters are present. */
};                                                                              
                    
struct rte_hash; 


#endif /* _RTE_HASH_H_ */
