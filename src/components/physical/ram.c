#include "components/physical/ram.h"
#include "components/logical/worker.h"
#include "collection/collection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void ram_init(RAM *ram, Worker *worker, double capacity, DSECollection *coll) {
    actor_init(&ram->base, ACTOR_RAM, coll, ram_run_profiler);
    ram->worker = worker;
    ram->capacity = capacity;
    ram->utilized = 0.0;
    ram->integrated_utilization = 0.0;
    ram->ongoing_count = 0;
    ram->ongoing_capacity = 64;
    ram->ongoing = (RamReservation *)malloc(ram->ongoing_capacity * sizeof(RamReservation));
    ram->next_reservation_id = 0;
}

void ram_free(RAM *ram) {
    free(ram->ongoing);
    ram->ongoing = NULL;
}

double ram_available(const RAM *ram) {
    return ram->capacity - ram->utilized;
}

void ram_run_profiler(Actor *self, ProfileResult *out) {
    RAM *ram = (RAM *)self;
    out->count = 0;

    double current_time = ram->base.collection->clock.time;
    double last_profiled = ram->base.last_profiled;
    double total_time = current_time - last_profiled;
    if (total_time <= 0.0) return;

    // Partial utilization from ongoing reservations
    double partial = 0.0;
    for (int i = 0; i < ram->ongoing_count; i++) {
        double start = fmax(last_profiled, ram->ongoing[i].start_time);
        partial += (current_time - start) * ram->ongoing[i].num_bytes;
    }

    double total = ram->integrated_utilization + partial;
    double avg = total / total_time;
    ram->integrated_utilization = 0.0;

    char key_avg[96], key_ratio[96];
    snprintf(key_avg, sizeof(key_avg), "utilization_avg(%d)", ram->worker->base.id);
    snprintf(key_ratio, sizeof(key_ratio), "utilization_ratio(%d)", ram->worker->base.id);

    strncpy(out->entries[0].key, key_avg, sizeof(out->entries[0].key) - 1);
    out->entries[0].value = avg;
    strncpy(out->entries[1].key, key_ratio, sizeof(out->entries[1].key) - 1);
    out->entries[1].value = avg / ram->capacity;
    out->count = 2;
}

static uint64_t ram_add_reservation(RAM *ram, double start_time, double num_bytes) {
    if (ram->ongoing_count == ram->ongoing_capacity) {
        ram->ongoing_capacity *= 2;
        ram->ongoing = (RamReservation *)realloc(ram->ongoing,
            ram->ongoing_capacity * sizeof(RamReservation));
    }
    uint64_t id = ram->next_reservation_id++;
    int idx = ram->ongoing_count++;
    ram->ongoing[idx] = (RamReservation){.id = id, .start_time = start_time, .num_bytes = num_bytes};
    return id;
}

static void ram_remove_reservation_by_id(RAM *ram, uint64_t id) {
    for (int i = 0; i < ram->ongoing_count; i++) {
        if (ram->ongoing[i].id == id) {
            // Integrate utilization before removing
            double current_time = ram->base.collection->clock.time;
            double start = fmax(ram->base.last_profiled, ram->ongoing[i].start_time);
            ram->integrated_utilization += ram->ongoing[i].num_bytes * (current_time - start);

            // Free the RAM
            ram->utilized -= ram->ongoing[i].num_bytes;

            // Swap with last
            ram->ongoing_count--;
            if (i < ram->ongoing_count) {
                ram->ongoing[i] = ram->ongoing[ram->ongoing_count];
            }
            return;
        }
    }
}

// Inner operation (CPU or disk) completed — release the RAM and forward result
static void ram_wrap_done(void *raw, int result) {
    RamWrapCtx *ctx = (RamWrapCtx *)raw;
    ram_remove_reservation_by_id(ctx->ram, ctx->reservation_id);

    Continuation outer = ctx->outer;
    pool_release(&ctx->ram->base.collection->pool_ram_wrap, ctx);

    cont_invoke(outer, result);
}

// Hold RAM while an inner async operation runs, then free it on completion
void ram_reserve_pending(RAM *ram, double num_bytes,
                         RamInnerLaunchFn inner_launch_fn, void *inner_ctx,
                         Continuation outer) {
    if (ram_available(ram) < num_bytes) {
        cont_invoke(outer, DSE_5XX);
        return;
    }

    double start = ram->base.collection->clock.time;
    ram->utilized += num_bytes;
    uint64_t id = ram_add_reservation(ram, start, num_bytes);

    RamWrapCtx *wrap = (RamWrapCtx *)pool_alloc(&ram->base.collection->pool_ram_wrap);
    wrap->ram = ram;
    wrap->num_bytes = num_bytes;
    wrap->reservation_id = id;
    wrap->outer = outer;

    // Launch the inner operation with our wrapper as its completion callback
    inner_launch_fn(inner_ctx, CONT(ram_wrap_done, wrap));
}
