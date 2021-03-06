#include <stdio.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <assert.h>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include "../core/clock.hpp"
#include "../core/logging.hpp"
#include <libcyclone.hpp>
#include <rte_launch.h>
#include "rocksdb.hpp"
#include "fb.hpp"

int driver(void *arg);

typedef struct driver_args_st {
  int leader;
  int me; 
  int mc;
  int replicas;
  int clients;
  int partitions;
  void **handles;
  void operator() ()
  {
    (void)driver((void *)this);
  }
} driver_args_t;

int driver(void *arg)
{
  driver_args_t *dargs = (driver_args_t *)arg;
  int me = dargs->me; 
  int mc = dargs->mc;
  int replicas = dargs->replicas;
  int clients = dargs->clients;
  int partitions = dargs->partitions;
  void **handles = dargs->handles;
  char *buffer = new char[DISP_MAX_MSGSIZE];
  struct proposal *prop = (struct proposal *)buffer;
  srand(time(NULL));
  int sz;
  rock_kv_t *resp;
  unsigned long order = 0;
  unsigned long tx_block_cnt   = 0;
  unsigned long tx_block_begin = rtc_clock::current_time();
  unsigned long total_latency  = 0;
  int rpc_flags;
  int my_core;
  fb_kv_t *kv = (fb_kv_t *)buffer;
  load_gen test(dargs->me);

  total_latency = 0;
  tx_block_cnt  = 0;
  tx_block_begin = rtc_clock::current_time();
  unsigned long tx_begin_time = rtc_clock::current_time();
  srand(tx_begin_time);
  int partition;
  while(true) {
    int idx = test.select_index();
    int sz;
    if(test.select_op() == 0) {
      rpc_flags = RPC_FLAG_RO;
      kv->op    = OP_GET;
      sz = sizeof(fb_kv_t);
    }
    else {
      rpc_flags = 0;
      kv->op    = OP_PUT;
      sz = sizeof(fb_kv_t) + VALUE_SIZE_ARR[idx];
    }
    kv->key   = ((unsigned long)idx) << 56;
    kv->key   = kv->key + test.gen_key(idx);
    my_core = kv->key % executor_threads;
    sz = make_rpc(handles[0],
		  buffer,
		  sz,
		  (void **)&resp,
		  1UL << my_core,
		  rpc_flags);
    tx_block_cnt++;
    
    if(dargs->leader) {
      if(tx_block_cnt > 5000) {
	total_latency = (rtc_clock::current_time() - tx_begin_time);
	BOOST_LOG_TRIVIAL(info) << "LOAD = "
				<< ((double)1000000*tx_block_cnt)/total_latency
				<< " tx/sec "
				<< "LATENCY = "
				<< ((double)total_latency)/tx_block_cnt
				<< " us ";
	tx_begin_time = rtc_clock::current_time();
	tx_block_cnt   = 0;
	total_latency  = 0;
      }
    }
  }
  return 0;
}

int main(int argc, const char *argv[]) {
  if(argc != 10) {
    printf("Usage: %s client_id_start client_id_stop mc replicas clients partitions cluster_config quorum_config_prefix server_ports\n", argv[0]);
    exit(-1);
  }
  
  int client_id_start = atoi(argv[1]);
  int client_id_stop  = atoi(argv[2]);
  driver_args_t *dargs;
  void **prev_handles;
  cyclone_network_init(argv[7], 1, atoi(argv[3]), 1 + client_id_stop - client_id_start);
  driver_args_t ** dargs_array = 
    (driver_args_t **)malloc((client_id_stop - client_id_start)*sizeof(driver_args_t *));
  for(int me = client_id_start; me < client_id_stop; me++) {
    dargs = (driver_args_t *) malloc(sizeof(driver_args_t));
    dargs_array[me - client_id_start] = dargs;
    if(me == client_id_start) {
      dargs->leader = 1;
    }
    else {
      dargs->leader = 0;
    }
    dargs->me = me;
    dargs->mc = atoi(argv[3]);
    dargs->replicas = atoi(argv[4]);
    dargs->clients  = atoi(argv[5]);
    dargs->partitions = atoi(argv[6]);
    dargs->handles = new void *[dargs->partitions];
    char fname_server[50];
    char fname_client[50];
    for(int i=0;i<dargs->partitions;i++) {
      sprintf(fname_server, "%s", argv[7]);
      sprintf(fname_client, "%s%d.ini", argv[8], i);
      dargs->handles[i] = cyclone_client_init(dargs->me,
					      dargs->mc,
					      1 + me - client_id_start,
					      fname_server,
					      atoi(argv[9]),
					      fname_client);
    }
  }
  for(int me = client_id_start; me < client_id_stop; me++) {
    int e = rte_eal_remote_launch(driver, dargs_array[me-client_id_start], 1 + me - client_id_start);
    if(e != 0) {
      BOOST_LOG_TRIVIAL(fatal) << "Failed to launch driver on remote lcore";
      exit(-1);
    }
  }
  rte_eal_mp_wait_lcore();
}
