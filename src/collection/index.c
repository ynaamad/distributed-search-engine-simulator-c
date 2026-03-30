#include "collection/index.h"
#include "components/logical/shard.h"
#include "components/logical/worker.h"
#include "collection/collection.h"
#include "util/rng.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

void index_init(Index *idx, DSECollection *coll, const char *name) {
    actor_init(&idx->base, ACTOR_INDEX, coll, index_run_profiler);
    strncpy(idx->name, name, sizeof(idx->name) - 1);
    idx->name[sizeof(idx->name) - 1] = '\0';
    dynarray_init(&idx->shards, 16);

    // Random hash parameters (matching Python's random initialization)
    idx->hash_a = rng_int(&coll->rng, 1, INDEX_HASH_P - 1);
    idx->hash_b = rng_int(&coll->rng, 0, INDEX_HASH_P - 1);

    idx->total_requests = 0;
    idx->total_latency = 0.0;
    idx->results_4xx = 0;
    idx->results_5xx = 0;
    idx->num_replicas = 0;
}

void index_free(Index *idx) {
    for (size_t i = 0; i < idx->shards.size; i++) {
        Shard *s = (Shard *)idx->shards.items[i];
        actor_delete(&s->base);
        free(s);
    }
    dynarray_free(&idx->shards);
}

int index_hash(const Index *idx, int document_id) {
    long long a = idx->hash_a;
    long long b = idx->hash_b;
    long long p = INDEX_HASH_P;
    long long h = ((a * (long long)document_id + b) % p + p) % p;
    return (int)h;
}

Shard *index_get_shard_for(const Index *idx, int document_id) {
    int h = index_hash(idx, document_id);
    int shard_idx = h % (int)idx->shards.size;
    return (Shard *)idx->shards.items[shard_idx];
}

Shard *index_get_shard_from_hash(const Index *idx, int hash) {
    int shard_idx = hash % (int)idx->shards.size;
    return (Shard *)idx->shards.items[shard_idx];
}

Shard *index_compute_shard(Index *idx, int document_id, int given_hash, bool has_given_hash) {
    if (has_given_hash) {
        if (given_hash < 0) {
            // Negative hash means direct shard index
            int shard_idx = (-given_hash - 1);
            return (Shard *)idx->shards.items[shard_idx % idx->shards.size];
        }
        int h = index_hash(idx, given_hash);
        return index_get_shard_from_hash(idx, h);
    }
    return index_get_shard_for(idx, document_id);
}

Shard *index_create_shard(Index *idx, Worker *worker) {
    Shard *s = (Shard *)malloc(sizeof(Shard));
    shard_init(s, idx, idx->base.collection);
    shard_assign_worker(s, worker);
    dynarray_push(&idx->shards, s);
    return s;
}

void index_create_shards(Index *idx, Worker *worker, int num_shards) {
    for (int i = 0; i < num_shards; i++) {
        index_create_shard(idx, worker);
    }
}

void index_create_shards_distributed(Index *idx, int num_shards) {
    DynArray *workers = collection_workers(idx->base.collection);
    if (workers->size == 0) return;

    for (int i = 0; i < num_shards; i++) {
        Worker *w = (Worker *)workers->items[i % workers->size];
        index_create_shard(idx, w);
    }
}

void index_transfer_shards_for_blue_green(Index *idx, Worker **new_workers, int num_new_workers) {
    if (num_new_workers == 0) return;

    int nw_idx = 0;
    for (size_t i = 0; i < idx->shards.size; i++) {
        Shard *s = (Shard *)idx->shards.items[i];
        // Reassign each worker in the shard to a new worker (round-robin)
        for (int j = 0; j < s->num_workers; j++) {
            Worker *new_w = new_workers[nw_idx % num_new_workers];
            nw_idx++;
            s->workers[j] = new_w;
            s->cpu_cycles[j] = 0.0;
            s->num_requests[j] = 0.0;
            s->size[j] = 0.0;
        }
    }
}

void index_log_result(Index *idx, int result, double latency) {
    idx->total_requests++;
    idx->total_latency += latency;
    if (result == DSE_4XX) idx->results_4xx++;
    else idx->results_5xx++;
}

void index_run_profiler(Actor *self, ProfileResult *out) {
    Index *idx = (Index *)self;
    out->count = 0;

    int c = 0;
    strncpy(out->entries[c].key, "total_requests", sizeof(out->entries[0].key));
    out->entries[c++].value = (double)idx->total_requests;

    double avg_latency = idx->total_requests > 0
        ? idx->total_latency / idx->total_requests : 0.0;
    strncpy(out->entries[c].key, "latency", sizeof(out->entries[0].key));
    out->entries[c++].value = avg_latency;

    strncpy(out->entries[c].key, "num_shards", sizeof(out->entries[0].key));
    out->entries[c++].value = (double)idx->shards.size;

    strncpy(out->entries[c].key, "4xx", sizeof(out->entries[0].key));
    out->entries[c++].value = (double)idx->results_4xx;

    strncpy(out->entries[c].key, "5xx", sizeof(out->entries[0].key));
    out->entries[c++].value = (double)idx->results_5xx;

    idx->total_requests = 0;
    idx->total_latency = 0.0;
    idx->results_4xx = 0;
    idx->results_5xx = 0;

    out->count = c;
}
