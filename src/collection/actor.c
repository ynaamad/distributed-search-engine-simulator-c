#include "collection/actor.h"
#include "collection/collection.h"
#include <stdio.h>
#include <string.h>

static int g_actor_counters[ACTOR_TYPE_COUNT] = {0};

int actor_next_id(ActorType type) {
    return ++g_actor_counters[type];
}

void actor_id_counters_reset(void) {
    memset(g_actor_counters, 0, sizeof(g_actor_counters));
}

static const char *ACTOR_TYPE_NAMES[] = {
    [ACTOR_CPU]      = "CPU",
    [ACTOR_CPU_POOL] = "CPUPool",
    [ACTOR_RAM]      = "RAM",
    [ACTOR_DISK]     = "Disk",
    [ACTOR_WORKER]   = "Worker",
    [ACTOR_SHARD]    = "Shard",
    [ACTOR_INDEX]    = "Index",
    [ACTOR_PROFILER] = "Profiler",
    [ACTOR_AGENT]    = "Agent",
};

const char *actor_type_name(ActorType type) {
    if (type >= 0 && type < ACTOR_TYPE_COUNT) return ACTOR_TYPE_NAMES[type];
    return "Unknown";
}

void actor_init(Actor *a, ActorType type, DSECollection *collection,
                void (*run_profiler)(Actor *, ProfileResult *)) {
    a->id = actor_next_id(type);
    a->type = type;
    a->collection = collection;
    a->run_profiler = run_profiler;
    a->last_profiled = 0.0;
    a->generation = 0;
    if (collection) {
        collection_register(collection, a);
    }
}

void actor_delete(Actor *a) {
    a->generation++;
    if (a->collection) {
        collection_unregister(a->collection, a);
    }
}

void actor_repr(const Actor *a, char *buf, size_t len) {
    snprintf(buf, len, "%s(%d)", actor_type_name(a->type), a->id);
}
