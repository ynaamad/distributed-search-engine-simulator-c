#ifndef DSE_CPU_H
#define DSE_CPU_H

#include "collection/actor.h"
#include "event/continuation.h"
#include "util/bounded_queue.h"
#include "util/dynarray.h"
#include "config.h"

typedef struct {
    double frequency;
} CPUSpec;

struct CPU {
    Actor     base;
    CPUPool  *pool;
    double    freq;
    double    last_reserved_time;   // -1.0 means not busy
    double    busy_time;            // integrated for profiling
    bool      enabled;
    bool      blue_green_active;
    double    blue_green_start;
    double    baseline_utilization;
    int       empty_profile_count;
};

// Context for an in-flight cpu_reserve operation
typedef struct {
    CPU          *cpu;
    double        cycles;
    double        duration;
    Continuation  caller;  // continuation to invoke on completion
} CPUReserveCtx;

struct CPUPool {
    Actor        base;
    Worker      *worker;
    CPUSpec      cpu_spec;
    DynArray     cpus;           // CPU* array
    DynArray     available_cpus; // CPU* subset currently idle
    bool         spinning_down;
    BoundedQueue wait_queue;     // stores Continuation structs (as void*)
};

// Context for CPUPool.spin_up (async operation with CPU_SPIN_UP_DELAY per CPU)
typedef struct {
    CPUPool      *pool;
    int           num_cpus;
    int           cpus_done;
    Continuation  caller;
} CPUPoolSpinUpCtx;

// CPU functions
void cpu_init(CPU *cpu, CPUPool *pool, double freq, DSECollection *coll);
void cpu_run_profiler(Actor *self, ProfileResult *out);
void cpu_start_blue_green(CPU *cpu);

// Async: reserve CPU for `cycles`, invoke `cont` on completion
void cpu_reserve(CPU *cpu, double cycles, Continuation cont);

// CPUPool functions
void cpu_pool_init(CPUPool *pool, Worker *worker, int num_initial_cpus,
                   CPUSpec spec, int queue_capacity, DSECollection *coll);
void cpu_pool_free(CPUPool *pool);

// Async: spin up a pool with N CPUs (each with CPU_SPIN_UP_DELAY)
void cpu_pool_spin_up_async(CPUPool *pool, Worker *worker, int num_cpus,
                            CPUSpec spec, int queue_capacity,
                            DSECollection *coll, Continuation cont);

// Async: reserve a CPU from the pool for `cycles`
// Returns immediately if CPU available; queues if not; returns 5xx if queue full.
void cpu_pool_reserve(CPUPool *pool, double cycles, Continuation cont);

// Release a CPU back to the pool (called when CPU reservation completes)
void cpu_pool_release(CPUPool *pool, CPU *cpu);

// Async: spin down the pool
void cpu_pool_spin_down(CPUPool *pool, Continuation cont);

#endif // DSE_CPU_H
