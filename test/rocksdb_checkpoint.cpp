#include<assert.h>
#include<errno.h>
#include<string.h>
#include<stdlib.h>
#include "../core/logging.hpp"
#include "../core/clock.hpp"
#include<stdio.h>
#include <time.h>
#include<unistd.h>
#include <rocksdb/db.h>
#include <rocksdb/options.h>
#include <rocksdb/write_batch.h>
#include <rocksdb/utilities/checkpoint.h>
#include "rocksdb.hpp"

// Rate measurement stuff
rocksdb::DB* db = NULL;
const int BATCH_SIZE = 100;

void opendb(){
  rocksdb::Options options;
  options.create_if_missing = true;
  //options.error_if_exists   = true;
  auto env = rocksdb::Env::Default();
  env->set_affinity(0, 10); 
  env->SetBackgroundThreads(2, rocksdb::Env::LOW);
  env->SetBackgroundThreads(1, rocksdb::Env::HIGH);
  options.env = env;
  options.write_buffer_size = 1024 * 1024 * 256;
  options.target_file_size_base = 1024 * 1024 * 512;
  options.max_background_compactions = 2;
  options.max_background_flushes = 1;
  options.max_write_buffer_number = 3;
  //options.min_write_buffer_number_to_merge = max(num_threads/2, 1);
  //options.compaction_style = rocksdb::kCompactionStyleNone;
  //options.memtable_factory.reset(new rocksdb::VectorRepFactory(1000));
  rocksdb::Status s = rocksdb::DB::Open(options, preload_dir, &db);
  if (!s.ok()){
    BOOST_LOG_TRIVIAL(fatal) << s.ToString().c_str();
    exit(-1);
  }
  // Create checkpoint
  rocksdb::Checkpoint * checkpoint_ptr;
  s = rocksdb::Checkpoint::Create(db, &checkpoint_ptr);
  if (!s.ok()){
    BOOST_LOG_TRIVIAL(fatal) << s.ToString().c_str();
    exit(-1);
  }
  s = checkpoint_ptr->CreateCheckpoint(data_dir);
  if (!s.ok()){
    BOOST_LOG_TRIVIAL(fatal) << s.ToString().c_str();
    exit(-1);
  }
  delete checkpoint_ptr;
}

void closedb()
{
    delete db;
}

int main(int argc, char *argv[])
{
  opendb();
  closedb();
}

