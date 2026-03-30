#ifndef DSE_SHARD_H
#define DSE_SHARD_H

#include "collection/actor.h"
#include "types.h"

#define SHARD_MAX_WORKERS 16

struct Shard {
    Actor     base;
    Index    *index;
    Worker   *workers[SHARD_MAX_WORKERS];
    int       num_workers;

    // Per-worker metrics (indexed by position in workers[])
    double    cpu_cycles[SHARD_MAX_WORKERS];
    double    num_requests[SHARD_MAX_WORKERS];
    double    size[SHARD_MAX_WORKERS];
};

void shard_init(Shard *s, Index *index, DSECollection *coll);
void shard_run_profiler(Actor *self, ProfileResult *out);

void shard_assign_worker(Shard *s, Worker *w);

// Replace old_worker with new_worker in the replica set
void shard_reassign(Shard *s, Worker *old_worker, Worker *new_worker);

// Pick a random worker from replicas (using collection's RNG)
Worker *shard_any_worker(Shard *s);

// Primary worker (workers[0])
static inline Worker *shard_primary_worker(Shard *s) {
    return s->workers[0];
}

// Number of replicas (excluding primary)
static inline int shard_num_replicas(const Shard *s) {
    return s->num_workers > 1 ? s->num_workers - 1 : 0;
}

// Find worker index in shard (-1 if not found)
int shard_worker_index(const Shard *s, const Worker *w);

void shard_log_request(Shard *s, Worker *w);
void shard_log_compute(Shard *s, Worker *w, double cpu_cycles);
void shard_log_size(Shard *s, Worker *w, double sz);

#endif // DSE_SHARD_H
