#ifndef DSE_CLOCK_H
#define DSE_CLOCK_H

#include "event/pq.h"
#include "event/continuation.h"
#include <stdbool.h>
#include <stdint.h>

struct Clock {
    double        time;
    uint64_t      counter;
    PriorityQueue pq;
    bool          running;
};

void clock_init(Clock *clock);
void clock_free(Clock *clock);

// Schedule a continuation to fire at absolute time `target_time`.
void clock_schedule(Clock *clock, double target_time, Continuation cont);

// Schedule a continuation to fire after `duration` seconds from now.
void clock_schedule_delay(Clock *clock, double duration, Continuation cont);

// Run the main event loop until stopped or queue empty.
void clock_run(Clock *clock);

// Stop the clock (can be called from within a callback).
void clock_stop(Clock *clock);

#endif // DSE_CLOCK_H
