#ifndef DSE_RAM_H
#define DSE_RAM_H

#include "collection/actor.h"
#include "event/continuation.h"

typedef struct {
    uint64_t id;
    double   start_time;
    double   num_bytes;
} RamReservation;

struct RAM {
    Actor            base;
    Worker          *worker;
    double           capacity;
    double           utilized;
    double           integrated_utilization;
    RamReservation  *ongoing;
    int              ongoing_count;
    int              ongoing_capacity;
    uint64_t         next_reservation_id;
};

// Context for RAM wrapping: allocate RAM, run inner operation, free RAM on completion
typedef struct {
    RAM          *ram;
    double        num_bytes;
    uint64_t      reservation_id;  // unique ID, stable across swap-removes
    Continuation  outer;           // caller's continuation
} RamWrapCtx;

void ram_init(RAM *ram, Worker *worker, double capacity, DSECollection *coll);
void ram_free(RAM *ram);
void ram_run_profiler(Actor *self, ProfileResult *out);

double ram_available(const RAM *ram);

// Reserve `num_bytes` of RAM, then launch `inner_launch` which will invoke
// its continuation when the inner operation completes. The RAM is freed
// when the inner operation finishes, and then `outer` is invoked.
//
// inner_launch_fn: function that starts the inner async operation.
//   It receives inner_launch_ctx and a Continuation to invoke when done.
// Returns DSE_5XX immediately if insufficient RAM.
typedef void (*RamInnerLaunchFn)(void *inner_ctx, Continuation on_done);

void ram_reserve_pending(RAM *ram, double num_bytes,
                         RamInnerLaunchFn inner_launch_fn, void *inner_ctx,
                         Continuation outer);

#endif // DSE_RAM_H
