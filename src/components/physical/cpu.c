#include "components/physical/cpu.h"
#include "event/clock.h"
#include "collection/collection.h"
#include "components/logical/worker.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ─── CPU ─────────────────────────────────────────────────────────────────────

void cpu_init(CPU *cpu, CPUPool *pool, double freq, DSECollection *coll) {
    actor_init(&cpu->base, ACTOR_CPU, coll, cpu_run_profiler);
    cpu->pool = pool;
    cpu->freq = freq;
    cpu->last_reserved_time = -1.0;
    cpu->busy_time = 0.0;
    cpu->enabled = true;
    cpu->blue_green_active = false;
    cpu->blue_green_start = 0.0;
    cpu->baseline_utilization = 0.0;
    cpu->empty_profile_count = 0;
}

void cpu_run_profiler(Actor *self, ProfileResult *out) {
    CPU *cpu = (CPU *)self;
    out->count = 0;

    bool busy = (cpu->last_reserved_time >= 0.0);

    if (cpu->busy_time == 0.0 && !busy) {
        cpu->empty_profile_count++;
        if (cpu->empty_profile_count >= 3) return;
    }

    double current_time = cpu->base.collection->clock.time;
    double total_time = current_time - cpu->base.last_profiled;
    if (total_time <= 0.0) return;

    double completed_duration = cpu->busy_time;
    cpu->busy_time = 0.0;

    double partial_duration = 0.0;
    if (busy) {
        partial_duration = (current_time - cpu->last_reserved_time) * (1.0 - cpu->baseline_utilization);
        cpu->last_reserved_time = current_time;
    }

    double utilization_frac = (completed_duration + partial_duration) / total_time;

    double bg_start = cpu->blue_green_active ? cpu->blue_green_start : current_time;
    double baseline = cpu->baseline_utilization *
        (current_time - fmax(cpu->base.last_profiled, bg_start)) / total_time;

    // Key format: "utilization_frac(Worker(N))"
    char key[96];
    assert(cpu->pool != NULL && cpu->pool->worker != NULL);
    snprintf(key, sizeof(key), "utilization_frac(%d)", cpu->pool->worker->base.id);

    out->entries[0] = (ProfileKV){.value = baseline + utilization_frac};
    strncpy(out->entries[0].key, key, sizeof(out->entries[0].key) - 1);
    out->count = 1;
}

void cpu_start_blue_green(CPU *cpu) {
    assert(!cpu->blue_green_active);
    cpu->blue_green_active = true;
    cpu->blue_green_start = cpu->base.collection->clock.time;

    double bg_util = cpu->base.collection->config.blue_green_cpu_util;
    cpu->freq *= (1.0 - bg_util);
    cpu->baseline_utilization = bg_util;
}

// CPU done processing cycles — re-add to pool, wake any queued request
static void cpu_reserve_done(void *raw, int) {
    CPUReserveCtx *ctx = (CPUReserveCtx *)raw;
    CPU *cpu = ctx->cpu;

    // Re-add to available if not shut down
    if (cpu->enabled) {
        dynarray_push(&cpu->pool->available_cpus, cpu);
    }

    double actual_duration = fmin(ctx->duration,
        cpu->base.collection->clock.time - cpu->last_reserved_time);
    cpu->busy_time += actual_duration * (1.0 - cpu->baseline_utilization);
    cpu->last_reserved_time = -1.0;

    Continuation caller = ctx->caller;
    DSECollection *coll = cpu->base.collection;
    pool_release(&coll->pool_cpu_reserve, ctx);

    // Release CPU back to pool (may wake waiting tasks)
    cpu_pool_release(cpu->pool, cpu);

    cont_invoke(caller, DSE_4XX);
}

// Occupy this CPU for `cycles` at its frequency, then invoke cont
void cpu_reserve(CPU *cpu, double cycles, Continuation cont) {
    if (!cpu->enabled) {
        cont_invoke(cont, DSE_ERR_CPU_SHUTDOWN);
        return;
    }
    assert(cpu->last_reserved_time < 0.0);

    double duration = cycles / cpu->freq;
    cpu->last_reserved_time = cpu->base.collection->clock.time;

    // Remove from available
    dynarray_remove(&cpu->pool->available_cpus, cpu);

    CPUReserveCtx *ctx = (CPUReserveCtx *)pool_alloc(&cpu->base.collection->pool_cpu_reserve);
    ctx->cpu = cpu;
    ctx->cycles = cycles;
    ctx->duration = duration;
    ctx->caller = cont;

    clock_schedule_delay(&cpu->base.collection->clock, duration, CONT(cpu_reserve_done, ctx));
}

// ─── CPUPool ─────────────────────────────────────────────────────────────────

void cpu_pool_init(CPUPool *pool, Worker *worker, int num_initial_cpus,
                   CPUSpec spec, int queue_capacity, DSECollection *coll) {
    actor_init(&pool->base, ACTOR_CPU_POOL, coll, NULL);
    pool->worker = worker;
    pool->cpu_spec = spec;
    pool->spinning_down = false;
    dynarray_init(&pool->cpus, 8);
    dynarray_init(&pool->available_cpus, 8);
    bq_init(&pool->wait_queue, queue_capacity);

    // Create initial CPUs synchronously (no spin-up delay)
    for (int i = 0; i < num_initial_cpus; i++) {
        CPU *cpu = (CPU *)malloc(sizeof(CPU));
        cpu_init(cpu, pool, spec.frequency, coll);
        dynarray_push(&pool->cpus, cpu);
        dynarray_push(&pool->available_cpus, cpu);
    }
}

void cpu_pool_free(CPUPool *pool) {
    for (size_t i = 0; i < pool->cpus.size; i++) {
        CPU *cpu = (CPU *)pool->cpus.items[i];
        actor_delete(&cpu->base);
        free(cpu);
    }
    dynarray_free(&pool->cpus);
    dynarray_free(&pool->available_cpus);
    bq_free(&pool->wait_queue);
}

// One CPU finished its spin-up delay — create it and add to pool
static void cpu_spin_up_one_done(void *raw, int) {
    CPUPoolSpinUpCtx *ctx = (CPUPoolSpinUpCtx *)raw;

    // Create and add the CPU
    CPU *cpu = (CPU *)malloc(sizeof(CPU));
    cpu_init(cpu, ctx->pool, ctx->pool->cpu_spec.frequency, ctx->pool->base.collection);
    dynarray_push(&ctx->pool->cpus, cpu);
    dynarray_push(&ctx->pool->available_cpus, cpu);

    ctx->cpus_done++;
    if (ctx->cpus_done == ctx->num_cpus) {
        Continuation caller = ctx->caller;
        free(ctx);
        cont_invoke(caller, DSE_OK);
    }
}

// Init a pool and spin up N CPUs concurrently (each with CPU_SPIN_UP_DELAY)
void cpu_pool_spin_up_async(CPUPool *pool, Worker *worker, int num_cpus,
                            CPUSpec spec, int queue_capacity,
                            DSECollection *coll, Continuation cont) {
    actor_init(&pool->base, ACTOR_CPU_POOL, coll, NULL);
    pool->worker = worker;
    pool->cpu_spec = spec;
    pool->spinning_down = false;
    dynarray_init(&pool->cpus, num_cpus);
    dynarray_init(&pool->available_cpus, num_cpus);
    bq_init(&pool->wait_queue, queue_capacity);

    if (num_cpus == 0) {
        cont_invoke(cont, DSE_OK);
        return;
    }

    // All CPUs spin up concurrently with the same delay
    // They all complete at the same time, but we use a gather to wait for all
    CPUPoolSpinUpCtx *ctx = (CPUPoolSpinUpCtx *)malloc(sizeof(CPUPoolSpinUpCtx));
    ctx->pool = pool;
    ctx->num_cpus = num_cpus;
    ctx->cpus_done = 0;
    ctx->caller = cont;

    double delay = coll->config.cpu_spin_up_delay;
    for (int i = 0; i < num_cpus; i++) {
        clock_schedule_delay(&coll->clock, delay, CONT(cpu_spin_up_one_done, ctx));
    }
}

// Context for a queued cpu_pool_reserve waiting for a CPU
typedef struct {
    CPUPool      *pool;
    double        cycles;
    Continuation  caller;
} CPUPoolReserveWaitCtx;

static void cpu_pool_reserve_got_cpu(void *raw, int);

// Get a CPU from the pool: use one immediately, queue if busy, 5xx if full
void cpu_pool_reserve(CPUPool *pool, double cycles, Continuation cont) {
    if (pool->available_cpus.size > 0) {
        // CPU available immediately
        CPU *cpu = (CPU *)pool->available_cpus.items[0];
        assert(cpu->last_reserved_time < 0.0);
        cpu_reserve(cpu, cycles, cont);
        return;
    }

    if (bq_full(&pool->wait_queue) || pool->wait_queue.finalized) {
        cont_invoke(cont, DSE_ERR_QUEUE_FULL);
        return;
    }

    // Queue the request — store a context with the continuation and cycles
    CPUPoolReserveWaitCtx *wait = (CPUPoolReserveWaitCtx *)malloc(sizeof(CPUPoolReserveWaitCtx));
    wait->pool = pool;
    wait->cycles = cycles;
    wait->caller = cont;

    Continuation *stored = (Continuation *)malloc(sizeof(Continuation));
    *stored = CONT(cpu_pool_reserve_got_cpu, wait);
    bq_push(&pool->wait_queue, stored);
}

// Woken from wait queue — grab an available CPU and start the reservation
static void cpu_pool_reserve_got_cpu(void *raw, int) {
    CPUPoolReserveWaitCtx *wait = (CPUPoolReserveWaitCtx *)raw;

    if (wait->pool->available_cpus.size == 0) {
        // This shouldn't happen, but handle gracefully
        cont_invoke(wait->caller, DSE_ERR_CPU_SHUTDOWN);
        free(wait);
        return;
    }

    CPU *cpu = (CPU *)wait->pool->available_cpus.items[0];
    assert(cpu->last_reserved_time < 0.0);

    Continuation caller = wait->caller;
    double cycles = wait->cycles;
    free(wait);

    cpu_reserve(cpu, cycles, caller);
}

// Return a CPU to the pool; if tasks are waiting, hand it to the next one
void cpu_pool_release(CPUPool *pool, CPU *cpu) {
    if ((cpu->enabled || pool->spinning_down) && !bq_empty(&pool->wait_queue)) {
        // Give CPU to next waiting task
        Continuation *stored = (Continuation *)bq_pop(&pool->wait_queue);
        if (stored) {
            Continuation caller = *stored;
            free(stored);
            caller.callback(caller.ctx, DSE_OK);
            return;
        }
    }
    // CPU stays in available_cpus (already re-added in cpu_reserve_done)
}

// Shut down the pool: finalize queue, fail all waiters, unregister
void cpu_pool_spin_down(CPUPool *pool, Continuation cont) {
    pool->spinning_down = true;
    bq_finalize(&pool->wait_queue);

    // Fail all waiting tasks
    while (!bq_empty(&pool->wait_queue)) {
        Continuation *stored = (Continuation *)bq_pop(&pool->wait_queue);
        if (stored) {
            Continuation caller = *stored;
            free(stored);
            cont_invoke(caller, DSE_ERR_WORKER_SHUTDOWN);
        }
    }

    actor_delete(&pool->base);
    cont_invoke(cont, DSE_OK);
}
