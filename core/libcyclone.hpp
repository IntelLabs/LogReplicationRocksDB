#ifndef _CYCLONE_HPP_
#define _CYCLONE_HPP_
#ifndef UINT64_MAX
#define UINT64_MAX (-1UL)
#endif
static const int DISP_MAX_MSGSIZE = 4096; 
//Note: DISP_MAX_MSGSIZE must be within MSG_MAXSIZE with room for rpc_t header
const int REP_UNKNOWN = 0;
const int REP_SUCCESS = 1;
const int REP_FAILED  = -1;


//Tuning parameters

// Server side timeouts -- usecs
static const int PERIODICITY                = 1; 
static const int RAFT_ELECTION_TIMEOUT      = 10000; 
static const int RAFT_QUORUM_TO             = 500;
static const int RAFT_REQUEST_TIMEOUT       = 1000; 
static const int RAFT_NACK_TIMEOUT          = 20;
// RAFT log tuning -- need to match load
static const int RAFT_LOG_TARGET  = 1000;

// Client side timeouts
static const int timeout_msec  = 30; // Client - failure detect

// Execution resources
static const int executor_threads = 32;

// ZMQ specific tuning
static const int zmq_threads = 4;

// DPDK specific tuning
static const int q_raft       = 0;
static const int q_dispatcher = 1;
static const int num_queues   = 2;
static const int num_quorums  = 8;
static const int Q_BUFS = 8191;
static const int R_BUFS = 1023;

// Maximum clients (1 million)
static const unsigned int MAX_CLIENTS = 1024U*1024U;
static const char MAX_INFLIGHT = 1;

static int core_to_quorum(int core_id)
{
  return core_id % num_quorums;
}


/////////////////////////////////

typedef struct rpc_cookie_st {
  int core_id;
  unsigned long core_mask;
  int log_idx;
  void *ret_value;
  int ret_size;
} rpc_cookie_t;

////// RPC Server side interface
typedef 
void (*rpc_callback_t)(const unsigned char *data,
		       const int len,
		       rpc_cookie_t * rpc_cookie);

// Add a flashlog entry
// Returns currently checkpointed log idx
typedef
int (*flashlog_callback_t)(const unsigned char *data,
			   const int len,
			   rpc_cookie_t *rpc_cookie);

//Garbage collect return value
typedef void (*rpc_gc_callback_t)(rpc_cookie_t *cookie);

// Callbacks structure
typedef struct rpc_callbacks_st {
  rpc_callback_t rpc_callback;
  rpc_gc_callback_t gc_callback;
  flashlog_callback_t flashlog_callback;
} rpc_callbacks_t;

// Init network stack
void cyclone_network_init(const char *config_cluster_path,
			  int ports,
			  int me_mc,
			  int queues);

// Start the dispatcher loop -- note: does not return
void dispatcher_start(const char* config_cluster_path,
		      const char* config_quorum_path,
		      rpc_callbacks_t *rpc_callbacks,
		      int me,
		      int me_mc,
		      int clients);

////// RPC client side interface
void* cyclone_client_init(int client_id,
			  int client_mc,
			  int client_queue,
			  const char *config_cluster_path,
			  int server_ports,
			  const char *config_quorum_path);
// Make an rpc call -- returns size of response
int make_rpc(void *handle,
	     void *payload,
	     int sz,
	     void **response,
	     unsigned long core_mask,
	     int rpc_flags);

int delete_node(void *handle, unsigned long core_mask, int node);

int add_node(void *handle, unsigned long core_mask, int node);


// Possible flags 
static const int RPC_FLAG_RO            = 1; // Read-only RPC


////// RocksDB parameters
const int rocksdb_num_threads           = 16;



/////////////////// Flash log interfaces /////////////////////
static const int flashlog_pagesize = (128*1024);
static const int flashlog_hwm = 200;
static const int flashlog_use_osync = 0;
static const unsigned long flashlog_segsize   =  (1024*1024*1024);
void *create_flash_log(const char *path);
int log_append(void *log_, 
	       const char *data, 
	       int size,
	       int raft_idx);

#endif
