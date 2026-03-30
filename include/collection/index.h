#ifndef DSE_INDEX_H
#define DSE_INDEX_H

#include "collection/actor.h"
#include "event/continuation.h"
#include "types.h"
#include "util/dynarray.h"

#define INDEX_HASH_P 1610612741

struct Index {
    Actor          base;
    char           name[64];
    DynArray       shards;     // Shard* array
    int            hash_a;
    int            hash_b;
    int            total_requests;
    double         total_latency;
    int            results_4xx;
    int            results_5xx;
    int            num_replicas;
};

void index_init(Index *idx, DSECollection *coll, const char *name);
void index_free(Index *idx);
void index_run_profiler(Actor *self, ProfileResult *out);

// Hash function for document routing
int index_hash(const Index *idx, int document_id);

// Get shard for a document_id
Shard *index_get_shard_for(const Index *idx, int document_id);

// Get shard from a pre-computed hash
Shard *index_get_shard_from_hash(const Index *idx, int hash);

// Compute shard — handles given_hash logic from Python
Shard *index_compute_shard(Index *idx, int document_id, int given_hash, bool has_given_hash);

// Create a shard and assign it to a worker
Shard *index_create_shard(Index *idx, Worker *worker);

// Create N shards distributed across workers
void index_create_shards(Index *idx, Worker *worker, int num_shards);

// Create shards distributed round-robin across all workers in collection
void index_create_shards_distributed(Index *idx, int num_shards);

// Blue-green shard transfer: reassign all shards to new workers (round-robin)
void index_transfer_shards_for_blue_green(Index *idx, Worker **new_workers, int num_new_workers);

// Log a completed request
void index_log_result(Index *idx, int result, double latency);

#endif // DSE_INDEX_H
