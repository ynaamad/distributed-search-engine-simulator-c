#ifndef DSE_COLLECTION_H
#define DSE_COLLECTION_H

#include "types.h"
#include "config.h"
#include "event/clock.h"
#include "util/dynarray.h"
#include "util/rng.h"
#include "util/pool.h"
#include "collection/actor.h"

#define DSE_MAX_INDICES 16

typedef struct {
    char   name[64];
    Index *index;
} IndexEntry;

struct DSECollection {
    char                name[64];
    DSECollectionConfig dse_config;
    Config              config;
    Clock               clock;
    RNG                 rng;

    DynArray            actors;
    DynArray            actors_by_type[ACTOR_TYPE_COUNT];

    IndexEntry          index_entries[DSE_MAX_INDICES];
    int                 num_indices;

    bool                steady;
    FILE               *log_file;

    // Per-type free-list pools for hot-path context structs
    Pool pool_cpu_reserve;       // CPUReserveCtx
    Pool pool_ram_wrap;          // RamWrapCtx
    Pool pool_cpu_inner;         // CpuInnerCtx
    Pool pool_disk_inner;        // DiskInnerCtx
    Pool pool_disk_reserve;      // DiskReserveCtx
    Pool pool_compute_ctx;       // WorkerReserveComputeCtx
    Pool pool_disk_ctx;          // WorkerReserveDiskCtx
};

// Initialize/free
void collection_init(DSECollection *c, const char *name,
                     DSECollectionConfig dse_config, Config config);
void collection_free(DSECollection *c);

// Actor registration
void collection_register(DSECollection *c, Actor *actor);
void collection_unregister(DSECollection *c, Actor *actor);

// Get actors by type
DynArray *collection_get_by_type(DSECollection *c, ActorType type);

// Index management
Index *collection_get_index(DSECollection *c, const char *name);
void   collection_add_index_entry(DSECollection *c, const char *name, Index *idx);

// Convenience: get all workers
// Returns pointer to the DynArray of Worker* actors
static inline DynArray *collection_workers(DSECollection *c) {
    return &c->actors_by_type[ACTOR_WORKER];
}

// Scaling operations (async)
void collection_scale_to(DSECollection *c, int num_workers,
                         int half_ocus_per_worker, Continuation cont);
void collection_scale_up(DSECollection *c, int count, Continuation cont);
void collection_scale_down(DSECollection *c, int count, Continuation cont);
void collection_scale_out(DSECollection *c, int count, Continuation cont);
void collection_scale_in(DSECollection *c, int count, Continuation cont);

#endif // DSE_COLLECTION_H
