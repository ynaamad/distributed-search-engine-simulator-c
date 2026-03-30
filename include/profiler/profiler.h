#ifndef DSE_PROFILER_H
#define DSE_PROFILER_H

#include "collection/actor.h"
#include "event/continuation.h"
#include "types.h"
#include <stdio.h>

// A single profiler history entry
typedef struct {
    double time;
    char   actor[64];
    char   key[96];
    double value;
} ProfileEntry;

struct Profiler {
    Actor          base;
    ProfileEntry  *history;
    int            history_count;
    int            history_capacity;
    Agent        **agents;
    int            num_agents;
    FILE          *save_file;
    double         period;
    bool           active;
};

void profiler_init(Profiler *p, DSECollection *coll, const char *save_path,
                   Agent **agents, int num_agents);
void profiler_free(Profiler *p);

// Start periodic profiling (self-scheduling)
void profiler_run_periodically(Profiler *p, double period, double first_delay);

// Run one profiling pass now
void profiler_profile(Profiler *p);

// Print a single metric line
void profiler_print(Profiler *p, double time, const char *actor,
                    const char *key, double value);

#endif // DSE_PROFILER_H
