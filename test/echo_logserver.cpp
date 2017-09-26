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

// Rate measurement stuff
static unsigned long *marks;
static unsigned long *completions;

static void *logs[executor_threads];

void callback(const unsigned char *data,
	      const int len,
	      rpc_cookie_t *cookie)
{
  cookie->ret_value  = NULL;
  cookie->ret_size   = 0;
  cookie->ret_value  = malloc(len);
  cookie->ret_size   = len;
  memcpy(cookie->ret_value, data, len);
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
  int idx = log_append(logs[cookie->core_id],
		       (const char *)data,
		       len, 
		       cookie->log_idx);
  return idx;
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
  int server_id = atoi(argv[1]);
  char log_path[50];
  for(int i=0;i<executor_threads;i++) {
    sprintf(log_path, "/mnt/ssd/flash_log%d", i);
    logs[i] = create_flash_log(log_path);
  }
  cyclone_network_init(argv[4],
		       atoi(argv[6]),
		       atoi(argv[2]),
		       atoi(argv[6]) + num_queues*num_quorums + executor_threads);
  dispatcher_start(argv[4], 
		   argv[5], 
		   &rpc_callbacks,
		   server_id, 
		   atoi(argv[2]), 
		   atoi(argv[3]));
}


