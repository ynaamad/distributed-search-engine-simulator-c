#ifndef DSE_WORKER_H
#define DSE_WORKER_H

#include "collection/actor.h"
#include "event/continuation.h"
#include "types.h"

// Forward declares for physical components (full defs in cpu.h, ram.h, disk.h)

struct Worker {
    Actor         base;
    CPUPool      *cpu_pool;
    RAM          *ram;
    Disk         *disk;
    int           total_requests;
    int           results_4xx;
    int           results_5xx;
    int           queue_length;
    int           request_count;
    WorkerStatus  status;
};

// Worker reserve_compute state machine context
typedef struct {
    Worker       *worker;
    double        cpu_cycles;
    double        mem_bytes;
    int           phase;
    Continuation  caller;
    int           queue_delta;
} WorkerReserveComputeCtx;

// Worker reserve_disk state machine context
typedef struct {
    Worker       *worker;
    double        bytes;
    DiskOp        op;
    Continuation  caller;
} WorkerReserveDiskCtx;

// Inner launch context for RAM wrapping of CPU reserve
typedef struct {
    CPUPool *pool;
    double   cycles;
} CpuInnerCtx;

// Inner launch context for RAM wrapping of disk reserve
typedef struct {
    Disk   *disk;
    double  bytes;
    DiskOp  op;
} DiskInnerCtx;

// Initialize a worker with initial CPUs (synchronous, no spin-up delay)
void worker_init(Worker *w, DSECollection *coll, int initial_cpus,
                 double cpu_freq, double memory_bytes, int queue_capacity,
                 WorkerStatus status);
void worker_free(Worker *w);

void worker_run_profiler(Actor *self, ProfileResult *out);

// Async: reserve compute (CPU + RAM), invoke cont with DSE_4XX or DSE_5XX
void worker_reserve_compute(Worker *w, double cpu_cycles, double mem_bytes,
                            Continuation cont);

// Async: reserve disk (disk + RAM), invoke cont with DSE_4XX or DSE_5XX
void worker_reserve_disk(Worker *w, DiskOp op, double bytes, Continuation cont);

// Async: spin up `count` new workers with green status
// Workers are allocated and returned via the continuation's context
typedef struct {
    DSECollection *coll;
    int            count;
    int            initial_cpus;
    double         cpu_freq;
    double         memory_bytes;
    int            queue_capacity;
    Worker       **workers;    // array of created workers (allocated by this fn)
    int            pools_done;
    Continuation   caller;
} WorkerSpinUpCtx;

void workers_spin_up(DSECollection *coll, int count, int initial_cpus,
                     double cpu_freq, double memory_bytes, int queue_capacity,
                     Continuation cont);

// Async: spin down a worker
void worker_spin_down(Worker *w, Continuation cont);

void worker_log_request(Worker *w);
void worker_log_result(Worker *w, int result);

#endif // DSE_WORKER_H
