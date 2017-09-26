#include<assert.h>
#include<errno.h>
#include<libcyclone.hpp>
#include<string.h>
#include<stdlib.h>
#include "../core/logging.hpp"
#include "../core/clock.hpp"
#include<stdio.h>
#include <time.h>
#include<unistd.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include "rocksdb.hpp"
#include <rocksdb/write_batch.h>

// Rate measurement stuff
static unsigned long *marks;
static unsigned long *completions;
rocksdb::DB* db = NULL;
static void *logs[executor_threads];

typedef struct batch_barrier_st {
  volatile unsigned long batch_barrier[2];
  volatile int batch_barrier_sense;
} batch_barrier_t;

static batch_barrier_t barriers[executor_threads];

static void barrier(batch_barrier_t *barrier,
		    int thread_id, 
		    unsigned long mask, 
		    bool leader)
{
  int sense = barrier->batch_barrier_sense;
  __sync_fetch_and_or(&barrier->batch_barrier[sense], 1UL << thread_id);
  if(leader) {
    while(barrier->batch_barrier[sense] != mask);
    barrier->batch_barrier_sense  = 1 - barrier->batch_barrier_sense;
    barrier->batch_barrier[sense] = 0;
  }
  else {
    while(barrier->batch_barrier[sense] != 0);
  }
}

void callback(const unsigned char *data,
	      const int len,
	      rpc_cookie_t *cookie)
{
  cookie->ret_value  = malloc(len);
  cookie->ret_size   = len;
  rock_kv_t *rock = (rock_kv_t *)data;
  if(rock->op == OP_PUT) {
    rocksdb::WriteOptions write_options;
    if(use_rocksdbwal) {
      write_options.sync       = true;
      write_options.disableWAL = false;
    }
    else {
      write_options.sync       = false;
      write_options.disableWAL = true;
    }
    if(len == sizeof(rock_kv_t)) { // single put
      rocksdb::Slice key((const char *)&rock->key, 8);
      rocksdb::Slice value((const char *)&rock->value[0], value_sz);
      rocksdb::Status s = db->Put(write_options, 
				  key,
				  value);
      if (!s.ok()){
	BOOST_LOG_TRIVIAL(fatal) << s.ToString();
	exit(-1);
      }
    }
    else {
      int leader = __builtin_ffsl(cookie->core_mask) - 1;
      if(leader == cookie->core_id) { // Multi put
	rocksdb::WriteBatch batch;
	int bytes  = len;
	const unsigned char *buffer = data;
	while(bytes) {
	  if(bytes == len) {
	    rocksdb::Slice key((const char *)&rock->key, 8);
	    rocksdb::Slice value((const char *)&rock->value[0], value_sz);
	    batch.Put(key, value);
	    buffer = buffer + sizeof(rock_kv_t);
	    bytes -= sizeof(rock_kv_t);
	  }
	  else {
	    rock_kv_pair_t *kv = (rock_kv_pair_t *)buffer;
	    rocksdb::Slice key((const char *)&kv->key, 8);
	    rocksdb::Slice value((const char *)&kv->value[0], value_sz);
	    batch.Put(key, value);
	    buffer = buffer + sizeof(rock_kv_pair_t);
	    bytes -= sizeof(rock_kv_pair_t);
	  }
	}
	rocksdb::Status s = db->Write(write_options, 
				      &batch);
	if (!s.ok()){
	  BOOST_LOG_TRIVIAL(fatal) << s.ToString();
	  exit(-1);
	}
	barrier(&barriers[leader], cookie->core_id, cookie->core_mask, true);
      }
      else {
	barrier(&barriers[leader], cookie->core_id, cookie->core_mask, false);
      }
    }
    memcpy(cookie->ret_value, data, len);
  }
  else {
    rock_kv_t *rock_back = (rock_kv_t *)cookie->ret_value;
    rocksdb::Slice key((const char *)&rock->key, 8);
    std::string value;
    rocksdb::Status s = db->Get(rocksdb::ReadOptions(),
				key,
				&value);
    if(s.IsNotFound()) {
      rock_back->key = ULONG_MAX;
    }
    else {
      rock_back->key = rock->key;
      memcpy(rock_back->value, value.c_str(), value_sz);
    }
  }
  /*
  if((++completions[cookie->core_id]) >= 1000000) {
    BOOST_LOG_TRIVIAL(info) << "Completion rate = "
			    << ((double)completions[cookie->core_id])
      /(rtc_clock::current_time() - marks[cookie->core_id]);
    completions[cookie->core_id] = 0;
    marks[cookie->core_id] = rtc_clock::current_time();
  }
  */
}

int wal_callback(const unsigned char *data,
		 const int len,
		 rpc_cookie_t *cookie)
{
  if(use_flashlog) {
    int idx = log_append(logs[cookie->core_id],
			 (const char *)data,
			 len, 
			 cookie->log_idx);
    return idx;
  }
  else {
    return cookie->log_idx;
  }
}

void gc(rpc_cookie_t *cookie)
{
  free(cookie->ret_value);
}

rpc_callbacks_t rpc_callbacks =  {
  callback,
  gc,
  wal_callback
};



void opendb(){
  rocksdb::Options options;
  int num_threads=rocksdb_num_threads;
  options.create_if_missing = true;
  options.write_buffer_size = 1024 * 1024 * 256;
  options.target_file_size_base = 1024 * 1024 * 512;
  options.IncreaseParallelism(num_threads);
  options.max_background_compactions = num_threads;
  options.max_background_flushes = num_threads;
  options.max_write_buffer_number = num_threads;
  options.wal_dir = log_dir;
  options.env->set_affinity(num_quorums + executor_threads, 
			    num_quorums + executor_threads + num_threads);
  rocksdb::Status s = rocksdb::DB::Open(options, data_dir, &db);
  if (!s.ok()){
    BOOST_LOG_TRIVIAL(fatal) << s.ToString().c_str();
    exit(-1);
  }
}

int main(int argc, char *argv[])
{
  if(argc != 7) {
    printf("Usage1: %s replica_id replica_mc clients cluster_config quorum_config ports\n", argv[0]);
    exit(-1);
  }
  marks       = (unsigned long *)malloc(executor_threads*sizeof(unsigned long));
  completions = (unsigned long *)malloc(executor_threads*sizeof(unsigned long));
  memset(marks, 0, executor_threads*sizeof(unsigned long));
  memset(completions, 0, executor_threads*sizeof(unsigned long));
  for(int i=0;i<executor_threads;i++) {
    barriers[i].batch_barrier[0] = 0;
    barriers[i].batch_barrier[1] = 0;
    barriers[i].batch_barrier_sense = 0;
  }
  int server_id = atoi(argv[1]);
  cyclone_network_init(argv[4],
		       atoi(argv[6]),
		       atoi(argv[2]),
		       atoi(argv[6]) + num_queues*num_quorums + executor_threads);
  

  char log_path[50];
  for(int i=0;i<executor_threads;i++) {
    sprintf(log_path, "%s/flash_log%d", log_dir, i);
    logs[i] = create_flash_log(log_path);
  }
  
  opendb();
  
  
  dispatcher_start(argv[4], 
		   argv[5], 
		   &rpc_callbacks,
		   server_id, 
		   atoi(argv[2]), 
		   atoi(argv[3]));
}


