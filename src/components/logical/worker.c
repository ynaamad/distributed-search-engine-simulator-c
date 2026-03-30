#include "components/logical/worker.h"
#include "components/physical/cpu.h"
#include "components/physical/ram.h"
#include "components/physical/disk.h"
#include "event/clock.h"
#include "collection/collection.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

void worker_init(Worker *w, DSECollection *coll, int initial_cpus,
                 double cpu_freq, double memory_bytes, int queue_capacity,
                 WorkerStatus status) {
    actor_init(&w->base, ACTOR_WORKER, coll, worker_run_profiler);
    w->total_requests = 0;
    w->results_4xx = 0;
    w->results_5xx = 0;
    w->queue_length = 0;
    w->request_count = 0;
    w->status = status;

    // Allocate and init physical components
    w->cpu_pool = (CPUPool *)malloc(sizeof(CPUPool));
    cpu_pool_init(w->cpu_pool, w, initial_cpus,
                  (CPUSpec){.frequency = cpu_freq}, queue_capacity, coll);

    w->ram = (RAM *)malloc(sizeof(RAM));
    ram_init(w->ram, w, memory_bytes, coll);

    w->disk = (Disk *)malloc(sizeof(Disk));
    disk_init(w->disk, w, coll->config.disk_read_speed, coll->config.disk_write_speed, coll);
}

void worker_free(Worker *w) {
    if (w->cpu_pool) {
        cpu_pool_free(w->cpu_pool);
        free(w->cpu_pool);
        w->cpu_pool = NULL;
    }
    if (w->ram) {
        ram_free(w->ram);
        free(w->ram);
        w->ram = NULL;
    }
    if (w->disk) {
        free(w->disk);
        w->disk = NULL;
    }
}

void worker_run_profiler(Actor *self, ProfileResult *out) {
    Worker *w = (Worker *)self;
    out->count = 0;

    int c = 0;
    strncpy(out->entries[c].key, "total_requests", sizeof(out->entries[c].key));
    out->entries[c++].value = (double)w->total_requests;
    w->total_requests = 0;

    strncpy(out->entries[c].key, "4xx", sizeof(out->entries[c].key));
    out->entries[c++].value = (double)w->results_4xx;

    strncpy(out->entries[c].key, "5xx", sizeof(out->entries[c].key));
    out->entries[c++].value = (double)w->results_5xx;

    w->results_4xx = 0;
    w->results_5xx = 0;

    strncpy(out->entries[c].key, "QueueLength", sizeof(out->entries[c].key));
    out->entries[c++].value = (double)w->queue_length;

    out->count = c;
}

void worker_log_request(Worker *w) {
    w->total_requests++;
}

void worker_log_result(Worker *w, int result) {
    if (result == DSE_4XX) w->results_4xx++;
    else w->results_5xx++;
}

// ─── reserve_compute state machine ──────────────────────────────────────────

// Starts the CPU reservation inside a RAM wrapper
static void cpu_inner_launch(void *raw, Continuation on_done) {
    CpuInnerCtx *ctx = (CpuInnerCtx *)raw;
    cpu_pool_reserve(ctx->pool, ctx->cycles, on_done);
    pool_release(&ctx->pool->base.collection->pool_cpu_inner, ctx);
}

// CPU+RAM done — decrement queue, forward success/failure to caller
static void reserve_compute_phase1_done(void *raw, int result) {
    WorkerReserveComputeCtx *ctx = (WorkerReserveComputeCtx *)raw;
    Worker *w = ctx->worker;

    w->queue_length -= ctx->queue_delta;

    int response = dse_is_error(result) ? DSE_5XX : DSE_4XX;
    Continuation caller = ctx->caller;
    pool_release(&w->base.collection->pool_compute_ctx, ctx);
    cont_invoke(caller, response);
}

// Reserve CPU cycles + RAM bytes on this worker (async)
void worker_reserve_compute(Worker *w, double cpu_cycles, double mem_bytes,
                            Continuation cont) {
    w->request_count++;

    // Fast-path rejection before allocating anything
    if (w->queue_length >= w->base.collection->dse_config.queue_length ||
        ram_available(w->ram) < mem_bytes) {
        cont_invoke(cont, DSE_5XX);
        return;
    }

    w->queue_length++;

    CpuInnerCtx *inner = (CpuInnerCtx *)pool_alloc(&w->base.collection->pool_cpu_inner);
    inner->pool = w->cpu_pool;
    inner->cycles = cpu_cycles;

    WorkerReserveComputeCtx *ctx = (WorkerReserveComputeCtx *)pool_alloc(&w->base.collection->pool_compute_ctx);
    ctx->worker = w;
    ctx->cpu_cycles = cpu_cycles;
    ctx->mem_bytes = mem_bytes;
    ctx->phase = 2;
    ctx->caller = cont;
    ctx->queue_delta = 1;

    ram_reserve_pending(w->ram, mem_bytes, cpu_inner_launch, inner,
                        CONT(reserve_compute_phase1_done, ctx));
}

// ─── reserve_disk ───────────────────────────────────────────────────────────

// Starts the disk I/O inside a RAM wrapper
static void disk_inner_launch(void *raw, Continuation on_done) {
    DiskInnerCtx *ctx = (DiskInnerCtx *)raw;
    if (ctx->op == DISK_READ) {
        disk_reserve_read(ctx->disk, ctx->bytes, on_done);
    } else {
        disk_reserve_write(ctx->disk, ctx->bytes, on_done);
    }
    pool_release(&ctx->disk->base.collection->pool_disk_inner, ctx);
}

static void reserve_disk_done(void *raw, int result) {
    WorkerReserveDiskCtx *ctx = (WorkerReserveDiskCtx *)raw;

    int response = dse_is_error(result) ? DSE_5XX : DSE_4XX;
    Continuation caller = ctx->caller;
    pool_release(&ctx->worker->base.collection->pool_disk_ctx, ctx);
    cont_invoke(caller, response);
}

// Reserve disk I/O + RAM on this worker (RAM held during the I/O)
void worker_reserve_disk(Worker *w, DiskOp op, double bytes, Continuation cont) {
    if (ram_available(w->ram) < bytes) {
        cont_invoke(cont, DSE_5XX);
        return;
    }

    DiskInnerCtx *inner = (DiskInnerCtx *)pool_alloc(&w->base.collection->pool_disk_inner);
    inner->disk = w->disk;
    inner->bytes = bytes;
    inner->op = op;

    WorkerReserveDiskCtx *ctx = (WorkerReserveDiskCtx *)pool_alloc(&w->base.collection->pool_disk_ctx);
    ctx->worker = w;
    ctx->bytes = bytes;
    ctx->op = op;
    ctx->caller = cont;

    ram_reserve_pending(w->ram, bytes, disk_inner_launch, inner,
                        CONT(reserve_disk_done, ctx));
}

// ─── Worker spin up (async) ─────────────────────────────────────────────────

static void workers_spin_up_pools_done(void *raw, int);
static void workers_spin_up_ready(void *raw, int);

// Create N green workers and spin up their CPU pools concurrently
void workers_spin_up(DSECollection *coll, int count, int initial_cpus,
                     double cpu_freq, double memory_bytes, int queue_capacity,
                     Continuation cont) {
    WorkerSpinUpCtx *ctx = (WorkerSpinUpCtx *)malloc(sizeof(WorkerSpinUpCtx));
    ctx->coll = coll;
    ctx->count = count;
    ctx->initial_cpus = initial_cpus;
    ctx->cpu_freq = cpu_freq;
    ctx->memory_bytes = memory_bytes;
    ctx->queue_capacity = queue_capacity;
    ctx->pools_done = 0;
    ctx->caller = cont;

    // Allocate workers (without CPUs — those spin up async)
    ctx->workers = (Worker **)malloc(count * sizeof(Worker *));
    for (int i = 0; i < count; i++) {
        Worker *w = (Worker *)malloc(sizeof(Worker));
        actor_init(&w->base, ACTOR_WORKER, coll, worker_run_profiler);
        w->total_requests = 0;
        w->results_4xx = 0;
        w->results_5xx = 0;
        w->queue_length = 0;
        w->request_count = 0;
        w->status = WORKER_GREEN;

        w->ram = (RAM *)malloc(sizeof(RAM));
        ram_init(w->ram, w, memory_bytes, coll);

        w->disk = (Disk *)malloc(sizeof(Disk));
        disk_init(w->disk, w, coll->config.disk_read_speed, coll->config.disk_write_speed, coll);

        w->cpu_pool = (CPUPool *)malloc(sizeof(CPUPool));
        ctx->workers[i] = w;
    }

    if (count == 0) {
        cont_invoke(cont, DSE_OK);
        free(ctx->workers);
        free(ctx);
        return;
    }

    // Spin up all CPU pools concurrently
    // Use a shared counter via ctx->pools_done
    for (int i = 0; i < count; i++) {
        cpu_pool_spin_up_async(ctx->workers[i]->cpu_pool, ctx->workers[i],
                               initial_cpus, (CPUSpec){.frequency = cpu_freq},
                               queue_capacity, coll,
                               CONT(workers_spin_up_pools_done, ctx));
    }
}

// One worker's CPU pool finished spinning up — check if all are done
static void workers_spin_up_pools_done(void *raw, int) {
    WorkerSpinUpCtx *ctx = (WorkerSpinUpCtx *)raw;
    ctx->pools_done++;
    if (ctx->pools_done == ctx->count) {
        // All pools ready, wait for WORKER_READY_TIME
        clock_schedule_delay(&ctx->coll->clock, ctx->coll->config.worker_ready_time,
                             CONT(workers_spin_up_ready, ctx));
    }
}

static void workers_spin_up_ready(void *raw, int) {
    WorkerSpinUpCtx *ctx = (WorkerSpinUpCtx *)raw;
    // Workers are ready — caller can access ctx->workers via the continuation
    // We pass the ctx pointer as context so the caller can extract workers
    cont_invoke(ctx->caller, DSE_OK);
    // Note: caller is responsible for freeing ctx->workers array and ctx
}

void worker_spin_down(Worker *w, Continuation cont) {
    assert(w->status == WORKER_OBSOLETE);
    cpu_pool_spin_down(w->cpu_pool, CONT_NULL);
    actor_delete(&w->base);
    cont_invoke(cont, DSE_OK);
}
