#include "components/physical/disk.h"
#include "components/logical/worker.h"
#include "event/clock.h"
#include "collection/collection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

void disk_init(Disk *disk, Worker *worker, double read_speed, double write_speed,
               DSECollection *coll) {
    actor_init(&disk->base, ACTOR_DISK, coll, disk_run_profiler);
    disk->worker = worker;
    disk->read_speed = read_speed;
    disk->write_speed = write_speed;
    disk->last_read_start = -1.0;
    disk->last_write_start = -1.0;
    disk->total_read = 0.0;
    disk->total_write = 0.0;
}

void disk_run_profiler(Actor *self, ProfileResult *out) {
    Disk *disk = (Disk *)self;
    out->count = 0;

    double current_time = disk->base.collection->clock.time;
    double total_time = current_time - disk->base.last_profiled;
    if (total_time <= 0.0) return;

    // Read metrics
    double read_frac = 0.0;
    if (disk->last_read_start >= 0.0) {
        read_frac = (current_time - disk->last_read_start) / total_time;
    }

    double write_frac = 0.0;
    if (disk->last_write_start >= 0.0) {
        write_frac = (current_time - disk->last_write_start) / total_time;
    }

    double read_bytes = disk->total_read;
    double write_bytes = disk->total_write;
    disk->total_read = 0.0;
    disk->total_write = 0.0;

    int c = 0;
    strncpy(out->entries[c].key, "read_utilization_frac", sizeof(out->entries[c].key));
    out->entries[c++].value = read_frac;
    strncpy(out->entries[c].key, "write_utilization_frac", sizeof(out->entries[c].key));
    out->entries[c++].value = write_frac;
    strncpy(out->entries[c].key, "read_bytes", sizeof(out->entries[c].key));
    out->entries[c++].value = read_bytes;
    strncpy(out->entries[c].key, "write_bytes", sizeof(out->entries[c].key));
    out->entries[c++].value = write_bytes;
    strncpy(out->entries[c].key, "read_rate_bytes", sizeof(out->entries[c].key));
    out->entries[c++].value = read_bytes / total_time;
    strncpy(out->entries[c].key, "write_rate_bytes", sizeof(out->entries[c].key));
    out->entries[c++].value = write_bytes / total_time;
    out->count = c;
}

static void disk_reserve_done(void *raw, int) {
    DiskReserveCtx *ctx = (DiskReserveCtx *)raw;
    Disk *disk = ctx->disk;

    if (ctx->op == DISK_READ) {
        disk->total_read += ctx->bytes;
        disk->last_read_start = -1.0;
    } else {
        disk->total_write += ctx->bytes;
        disk->last_write_start = -1.0;
    }

    Continuation caller = ctx->caller;
    pool_release(&disk->base.collection->pool_disk_reserve, ctx);
    cont_invoke(caller, DSE_4XX);
}

static void disk_reserve_op(Disk *disk, double bytes, DiskOp op, Continuation cont) {
    double speed = (op == DISK_READ) ? disk->read_speed : disk->write_speed;
    double duration = bytes / speed;

    if (op == DISK_READ) {
        disk->last_read_start = disk->base.collection->clock.time;
    } else {
        disk->last_write_start = disk->base.collection->clock.time;
    }

    DiskReserveCtx *ctx = (DiskReserveCtx *)pool_alloc(&disk->base.collection->pool_disk_reserve);
    ctx->disk = disk;
    ctx->bytes = bytes;
    ctx->op = op;
    ctx->caller = cont;

    clock_schedule_delay(&disk->base.collection->clock, duration, CONT(disk_reserve_done, ctx));
}

void disk_reserve_read(Disk *disk, double bytes, Continuation cont) {
    disk_reserve_op(disk, bytes, DISK_READ, cont);
}

void disk_reserve_write(Disk *disk, double bytes, Continuation cont) {
    disk_reserve_op(disk, bytes, DISK_WRITE, cont);
}
