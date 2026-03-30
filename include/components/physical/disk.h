#ifndef DSE_DISK_H
#define DSE_DISK_H

#include "collection/actor.h"
#include "event/continuation.h"
#include "types.h"

struct Disk {
    Actor    base;
    Worker  *worker;
    double   read_speed;
    double   write_speed;
    double   last_read_start;   // -1.0 if idle
    double   last_write_start;  // -1.0 if idle
    double   total_read;
    double   total_write;
};

typedef struct {
    Disk         *disk;
    double        bytes;
    DiskOp        op;
    Continuation  caller;
} DiskReserveCtx;

void disk_init(Disk *disk, Worker *worker, double read_speed, double write_speed,
               DSECollection *coll);
void disk_run_profiler(Actor *self, ProfileResult *out);

// Async: reserve disk for a read/write operation
void disk_reserve_read(Disk *disk, double bytes, Continuation cont);
void disk_reserve_write(Disk *disk, double bytes, Continuation cont);

#endif // DSE_DISK_H
