#include "event/clock.h"

void clock_init(Clock *clock) {
    clock->time = 0.0;
    clock->counter = 0;
    pq_init(&clock->pq, 4096);
    clock->running = false;
}

void clock_free(Clock *clock) {
    pq_free(&clock->pq);
}

void clock_schedule(Clock *clock, double target_time, Continuation cont) {
    clock->counter++;
    if (target_time < clock->time) target_time = clock->time;
    PQCont pc = {.callback = cont.callback, .ctx = cont.ctx};
    pq_push(&clock->pq, target_time, clock->counter, pc);
}

void clock_schedule_delay(Clock *clock, double duration, Continuation cont) {
    if (duration <= 0) {
        // Fire immediately at current time (but still goes through queue for ordering)
        clock_schedule(clock, clock->time, cont);
    } else {
        clock_schedule(clock, clock->time + duration, cont);
    }
}

void clock_run(Clock *clock) {
    clock->running = true;

    while (clock->running && !pq_empty(&clock->pq)) {
        PQEntry ev = pq_pop(&clock->pq);
        clock->time = ev.time;
        if (ev.cont.callback) {
            ev.cont.callback(ev.cont.ctx, DSE_OK);
        }
    }

    clock->running = false;
}

void clock_stop(Clock *clock) {
    clock->running = false;
}
