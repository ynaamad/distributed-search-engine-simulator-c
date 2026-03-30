#include "components/logical/shard.h"
#include "components/logical/worker.h"
#include "collection/collection.h"
#include "util/rng.h"
#include <string.h>
#include <stdio.h>
#include <assert.h>

void shard_init(Shard *s, Index *index, DSECollection *coll) {
    actor_init(&s->base, ACTOR_SHARD, coll, shard_run_profiler);
    s->index = index;
    s->num_workers = 0;
    memset(s->cpu_cycles, 0, sizeof(s->cpu_cycles));
    memset(s->num_requests, 0, sizeof(s->num_requests));
    memset(s->size, 0, sizeof(s->size));
}

void shard_assign_worker(Shard *s, Worker *w) {
    assert(s->num_workers < SHARD_MAX_WORKERS);
    s->workers[s->num_workers] = w;
    s->cpu_cycles[s->num_workers] = 0.0;
    s->num_requests[s->num_workers] = 0.0;
    s->size[s->num_workers] = 0.0;
    s->num_workers++;
}

int shard_worker_index(const Shard *s, const Worker *w) {
    for (int i = 0; i < s->num_workers; i++) {
        if (s->workers[i] == w) return i;
    }
    return -1;
}

void shard_reassign(Shard *s, Worker *old_worker, Worker *new_worker) {
    int idx = shard_worker_index(s, old_worker);
    assert(idx >= 0);
    s->workers[idx] = new_worker;
    s->cpu_cycles[idx] = 0.0;
    s->num_requests[idx] = 0.0;
    s->size[idx] = 0.0;
}

Worker *shard_any_worker(Shard *s) {
    assert(s->num_workers > 0);
    int idx = rng_choice_index(&s->base.collection->rng, s->num_workers);
    return s->workers[idx];
}

void shard_log_request(Shard *s, Worker *w) {
    int idx = shard_worker_index(s, w);
    if (idx >= 0) s->num_requests[idx] += 1.0;
}

void shard_log_compute(Shard *s, Worker *w, double cycles) {
    int idx = shard_worker_index(s, w);
    if (idx >= 0) s->cpu_cycles[idx] += cycles;
}

void shard_log_size(Shard *s, Worker *w, double sz) {
    int idx = shard_worker_index(s, w);
    if (idx >= 0) s->size[idx] += sz;
}

void shard_run_profiler(Actor *self, ProfileResult *out) {
    Shard *s = (Shard *)self;
    out->count = 0;

    for (int i = 0; i < s->num_workers && out->count + 3 <= PROFILE_MAX_ENTRIES; i++) {
        int wid = s->workers[i]->base.id;

        snprintf(out->entries[out->count].key, sizeof(out->entries[0].key),
                 "cpu_cycles(%d)", wid);
        out->entries[out->count].value = s->cpu_cycles[i];
        s->cpu_cycles[i] = 0.0;
        out->count++;

        snprintf(out->entries[out->count].key, sizeof(out->entries[0].key),
                 "num_requests(%d)", wid);
        out->entries[out->count].value = s->num_requests[i];
        s->num_requests[i] = 0.0;
        out->count++;

        snprintf(out->entries[out->count].key, sizeof(out->entries[0].key),
                 "size(%d)", wid);
        out->entries[out->count].value = s->size[i];
        s->size[i] = 0.0;
        out->count++;
    }
}
