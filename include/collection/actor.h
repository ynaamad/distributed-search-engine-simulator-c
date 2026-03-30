#ifndef DSE_ACTOR_H
#define DSE_ACTOR_H

#include "types.h"
#include "util/dynarray.h"
#include <stdio.h>

// Profile key-value pair
typedef struct {
    char   key[96];
    double value;
} ProfileKV;

#define PROFILE_MAX_ENTRIES 32

// Profiler result from run_profiler
typedef struct {
    ProfileKV entries[PROFILE_MAX_ENTRIES];
    int       count;
} ProfileResult;

// Actor base struct — embedded as first field in all component structs.
// run_profiler is NULL for non-profilable actors.
struct Actor {
    int            id;
    ActorType      type;
    DSECollection *collection;
    void         (*run_profiler)(Actor *self, ProfileResult *out);
    double         last_profiled;
    int            generation;  // incremented on delete, for stale continuation detection
};

// Per-type ID counters
int actor_next_id(ActorType type);
void actor_id_counters_reset(void);

// Initialize actor base fields and register with collection
void actor_init(Actor *a, ActorType type, DSECollection *collection,
                void (*run_profiler)(Actor *, ProfileResult *));

// Unregister from collection
void actor_delete(Actor *a);

// Format repr string like "CPU(3)" into buf
void actor_repr(const Actor *a, char *buf, size_t len);

// Get actor type name
const char *actor_type_name(ActorType type);

#endif // DSE_ACTOR_H
