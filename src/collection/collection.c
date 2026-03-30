#include "collection/collection.h"
#include "components/logical/worker.h"
#include "components/physical/cpu.h"
#include "components/physical/ram.h"
#include "components/physical/disk.h"
#include "collection/index.h"
#include "components/logical/shard.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

void collection_init(DSECollection *c, const char *name,
                     DSECollectionConfig dse_config, Config config) {
    memset(c, 0, sizeof(*c));
    strncpy(c->name, name, sizeof(c->name) - 1);
    c->dse_config = dse_config;
    c->config = config;
    c->steady = true;
    c->log_file = NULL;
    c->num_indices = 0;

    clock_init(&c->clock);
    rng_seed(&c->rng, 1);

    // Init context pools for hot-path allocations
    pool_init(&c->pool_cpu_reserve, sizeof(CPUReserveCtx));
    pool_init(&c->pool_ram_wrap, sizeof(RamWrapCtx));
    pool_init(&c->pool_cpu_inner, sizeof(CpuInnerCtx));
    pool_init(&c->pool_disk_inner, sizeof(DiskInnerCtx));
    pool_init(&c->pool_disk_reserve, sizeof(DiskReserveCtx));
    pool_init(&c->pool_compute_ctx, sizeof(WorkerReserveComputeCtx));
    pool_init(&c->pool_disk_ctx, sizeof(WorkerReserveDiskCtx));

    dynarray_init(&c->actors, 64);
    for (int i = 0; i < ACTOR_TYPE_COUNT; i++) {
        dynarray_init(&c->actors_by_type[i], 16);
    }

    // Create initial workers
    int cpus = dse_config_cpus_per_worker(&dse_config);
    double mem = dse_config_memory_per_worker(&dse_config, &config);

    for (int i = 0; i < dse_config.num_workers; i++) {
        Worker *w = (Worker *)malloc(sizeof(Worker));
        worker_init(w, c, cpus, dse_config.cpu_frequency, mem,
                    dse_config.queue_length, WORKER_BLUE);
    }
}

void collection_free(DSECollection *c) {
    // Free all indices
    for (int i = 0; i < c->num_indices; i++) {
        index_free(c->index_entries[i].index);
        free(c->index_entries[i].index);
    }

    // Free all workers (and their owned components)
    DynArray *workers = collection_workers(c);
    for (size_t i = 0; i < workers->size; i++) {
        Worker *w = (Worker *)workers->items[i];
        worker_free(w);
        free(w);
    }

    for (int i = 0; i < ACTOR_TYPE_COUNT; i++) {
        dynarray_free(&c->actors_by_type[i]);
    }
    dynarray_free(&c->actors);
    clock_free(&c->clock);

    pool_free_all(&c->pool_cpu_reserve);
    pool_free_all(&c->pool_ram_wrap);
    pool_free_all(&c->pool_cpu_inner);
    pool_free_all(&c->pool_disk_inner);
    pool_free_all(&c->pool_disk_reserve);
    pool_free_all(&c->pool_compute_ctx);
    pool_free_all(&c->pool_disk_ctx);
}

void collection_register(DSECollection *c, Actor *actor) {
    if (!dynarray_contains(&c->actors, actor)) {
        dynarray_push(&c->actors, actor);
        dynarray_push(&c->actors_by_type[actor->type], actor);
    }
}

void collection_unregister(DSECollection *c, Actor *actor) {
    dynarray_remove(&c->actors, actor);
    dynarray_remove(&c->actors_by_type[actor->type], actor);
}

DynArray *collection_get_by_type(DSECollection *c, ActorType type) {
    return &c->actors_by_type[type];
}

Index *collection_get_index(DSECollection *c, const char *name) {
    for (int i = 0; i < c->num_indices; i++) {
        if (strcmp(c->index_entries[i].name, name) == 0) {
            return c->index_entries[i].index;
        }
    }
    return NULL;
}

void collection_add_index_entry(DSECollection *c, const char *name, Index *idx) {
    assert(c->num_indices < DSE_MAX_INDICES);
    strncpy(c->index_entries[c->num_indices].name, name,
            sizeof(c->index_entries[0].name) - 1);
    c->index_entries[c->num_indices].index = idx;
    c->num_indices++;
}

// ─── Scaling ────────────────────────────────────────────────────────────────

typedef struct {
    DSECollection *coll;
    int            new_num_workers;
    int            new_half_ocus;
    int            phase;
    Worker       **old_workers;
    int            old_count;
    Worker       **new_workers;
    int            new_count;
    int            spin_downs_done;
    Continuation   caller;
} ScaleCtx;

static void scale_phase_workers_ready(void *raw, int);
static void scale_phase_spin_down_done(void *raw, int);

// Blue-green deploy: halve old CPUs, spin up new workers, rebalance, spin down old
static void scale_workers_start(DSECollection *c, int new_num_workers,
                                int new_half_ocus, Continuation cont) {
    if (!c->steady && !c->config.ignore_steady) {
        cont_invoke(cont, DSE_ERR_NOT_STEADY);
        return;
    }

    c->steady = false;

    ScaleCtx *ctx = (ScaleCtx *)malloc(sizeof(ScaleCtx));
    ctx->coll = c;
    ctx->new_num_workers = new_num_workers;
    ctx->new_half_ocus = new_half_ocus;
    ctx->caller = cont;

    // Gather old workers
    DynArray *workers = collection_workers(c);
    ctx->old_count = (int)workers->size;
    ctx->old_workers = (Worker **)malloc(ctx->old_count * sizeof(Worker *));
    for (int i = 0; i < ctx->old_count; i++) {
        ctx->old_workers[i] = (Worker *)workers->items[i];
    }

    // Mark old workers for blue-green
    for (int i = 0; i < ctx->old_count; i++) {
        Worker *w = ctx->old_workers[i];
        CPUPool *pool = w->cpu_pool;
        pool->cpu_spec.frequency /= 2.0;
        for (size_t j = 0; j < pool->cpus.size; j++) {
            CPU *cpu = (CPU *)pool->cpus.items[j];
            cpu_start_blue_green(cpu);
        }
    }

    // Update config
    c->dse_config.half_ocus_per_worker = new_half_ocus;
    c->dse_config.num_workers += new_num_workers; // temporarily higher during rebalance

    int cpus = dse_config_cpus_per_worker(&c->dse_config);
    double mem = dse_config_memory_per_worker(&c->dse_config, &c->config);

    // Spin up new workers async
    workers_spin_up(c, new_num_workers, cpus, c->dse_config.cpu_frequency,
                    mem, c->dse_config.queue_length,
                    CONT(scale_phase_workers_ready, ctx));
}

// New workers spun up — rebalance shards, swap statuses, spin down old workers
static void scale_phase_workers_ready(void *raw, int) {
    ScaleCtx *ctx = (ScaleCtx *)raw;
    DSECollection *c = ctx->coll;

    // Find the new workers (they're GREEN status)
    DynArray *workers = collection_workers(c);
    ctx->new_count = 0;
    ctx->new_workers = (Worker **)malloc(workers->size * sizeof(Worker *));
    for (size_t i = 0; i < workers->size; i++) {
        Worker *w = (Worker *)workers->items[i];
        if (w->status == WORKER_GREEN) {
            ctx->new_workers[ctx->new_count++] = w;
        }
    }

    // Rebalance shards
    for (int i = 0; i < c->num_indices; i++) {
        index_transfer_shards_for_blue_green(c->index_entries[i].index,
                                              ctx->new_workers, ctx->new_count);
    }

    // Update statuses
    for (int i = 0; i < ctx->new_count; i++) {
        ctx->new_workers[i]->status = WORKER_BLUE;
    }
    for (int i = 0; i < ctx->old_count; i++) {
        ctx->old_workers[i]->status = WORKER_OBSOLETE;
    }

    // Spin down old workers
    if (ctx->old_count == 0) {
        c->dse_config.num_workers = ctx->new_num_workers;
        c->steady = true;
        Continuation caller = ctx->caller;
        free(ctx->old_workers);
        free(ctx->new_workers);
        free(ctx);
        cont_invoke(caller, DSE_OK);
        return;
    }

    ctx->spin_downs_done = 0;
    for (int i = 0; i < ctx->old_count; i++) {
        worker_spin_down(ctx->old_workers[i], CONT(scale_phase_spin_down_done, ctx));
    }
}

// One old worker finished spinning down — when all done, finalize scaling
static void scale_phase_spin_down_done(void *raw, int) {
    ScaleCtx *ctx = (ScaleCtx *)raw;
    ctx->spin_downs_done++;
    if (ctx->spin_downs_done == ctx->old_count) {
        ctx->coll->dse_config.num_workers = ctx->new_num_workers;
        ctx->coll->steady = true;

        Continuation caller = ctx->caller;
        // Free old workers
        for (int i = 0; i < ctx->old_count; i++) {
            worker_free(ctx->old_workers[i]);
            free(ctx->old_workers[i]);
        }
        free(ctx->old_workers);
        free(ctx->new_workers);
        free(ctx);
        cont_invoke(caller, DSE_OK);
    }
}

void collection_scale_to(DSECollection *c, int num_workers,
                         int half_ocus_per_worker, Continuation cont) {
    if (!c->steady && !c->config.ignore_steady) {
        cont_invoke(cont, DSE_ERR_NOT_STEADY);
        return;
    }

    if (c->dse_config.num_workers == num_workers &&
        c->dse_config.half_ocus_per_worker == half_ocus_per_worker) {
        cont_invoke(cont, DSE_4XX);
        return;
    }

    if (num_workers < 1) {
        cont_invoke(cont, DSE_ERR_INVALID_SCALING);
        return;
    }
    if (half_ocus_per_worker < 1) {
        cont_invoke(cont, DSE_ERR_INVALID_SCALING);
        return;
    }

    scale_workers_start(c, num_workers, half_ocus_per_worker, cont);
}

void collection_scale_up(DSECollection *c, int count, Continuation cont) {
    collection_scale_to(c, c->dse_config.num_workers,
                        c->dse_config.half_ocus_per_worker + count, cont);
}

void collection_scale_down(DSECollection *c, int count, Continuation cont) {
    collection_scale_to(c, c->dse_config.num_workers,
                        c->dse_config.half_ocus_per_worker - count, cont);
}

void collection_scale_out(DSECollection *c, int count, Continuation cont) {
    collection_scale_to(c, c->dse_config.num_workers + count,
                        c->dse_config.half_ocus_per_worker, cont);
}

void collection_scale_in(DSECollection *c, int count, Continuation cont) {
    collection_scale_to(c, c->dse_config.num_workers - count,
                        c->dse_config.half_ocus_per_worker, cont);
}
