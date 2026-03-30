#include "profiler/profiler.h"
#include "collection/collection.h"
#include "components/logical/worker.h"
#include "agent/agent.h"
#include "event/clock.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void profiler_init(Profiler *p, DSECollection *coll, const char *save_path,
                   Agent **agents, int num_agents) {
    actor_init(&p->base, ACTOR_PROFILER, coll, NULL);
    p->history_count = 0;
    p->history_capacity = 4096;
    p->history = (ProfileEntry *)malloc(p->history_capacity * sizeof(ProfileEntry));
    p->agents = agents;
    p->num_agents = num_agents;
    p->active = true;
    p->period = 300.0;

    if (save_path) {
        p->save_file = fopen(save_path, "w");
    } else {
        p->save_file = NULL;
    }
}

void profiler_free(Profiler *p) {
    if (p->save_file) {
        fclose(p->save_file);
        p->save_file = NULL;
    }
    free(p->history);
    p->history = NULL;
    p->active = false;
}

void profiler_print(Profiler *p, double time, const char *actor,
                    const char *key, double value) {
    // Grow history if needed
    if (p->history_count == p->history_capacity) {
        p->history_capacity *= 2;
        p->history = (ProfileEntry *)realloc(p->history,
            p->history_capacity * sizeof(ProfileEntry));
    }

    ProfileEntry *e = &p->history[p->history_count++];
    e->time = time;
    strncpy(e->actor, actor, sizeof(e->actor) - 1);
    e->actor[sizeof(e->actor) - 1] = '\0';
    strncpy(e->key, key, sizeof(e->key) - 1);
    e->key[sizeof(e->key) - 1] = '\0';
    e->value = value;

    char line[256];
    snprintf(line, sizeof(line), "%.4f,%s,%s,%.4f\n", time, actor, key, value);

    if (p->save_file) {
        fputs(line, p->save_file);
    } else {
        fputs(line, stdout);
    }
}

void profiler_profile(Profiler *p) {
    DSECollection *c = p->base.collection;
    double time = c->clock.time;

    char coll_repr[96];
    snprintf(coll_repr, sizeof(coll_repr), "DSECollection(%s)", c->name);

    // Collection-level metrics
    profiler_print(p, time, coll_repr, "num_workers",
                   (double)c->dse_config.num_workers);

    int blue = 0, green = 0;
    DynArray *workers = collection_workers(c);
    for (size_t i = 0; i < workers->size; i++) {
        Worker *w = (Worker *)workers->items[i];
        if (w->status == WORKER_BLUE) blue++;
        else if (w->status == WORKER_GREEN) green++;
    }
    profiler_print(p, time, coll_repr, "blue_workers", (double)blue);
    profiler_print(p, time, coll_repr, "green_workers", (double)green);
    profiler_print(p, time, coll_repr, "ocus_per_worker",
                   (double)c->dse_config.half_ocus_per_worker / 2.0);
    profiler_print(p, time, coll_repr, "steady", c->steady ? 1.0 : 0.0);

    // Profile all profilable actors (sorted by repr for consistency)
    // Simple approach: iterate actors_by_type in order
    for (int t = 0; t < ACTOR_TYPE_COUNT; t++) {
        DynArray *arr = &c->actors_by_type[t];
        for (size_t i = 0; i < arr->size; i++) {
            Actor *a = (Actor *)arr->items[i];
            if (!a->run_profiler) continue;

            ProfileResult pr;
            a->run_profiler(a, &pr);
            a->last_profiled = time;

            char repr[64];
            actor_repr(a, repr, sizeof(repr));

            for (int k = 0; k < pr.count; k++) {
                profiler_print(p, time, repr, pr.entries[k].key, pr.entries[k].value);
            }
        }
    }

    if (p->save_file) fflush(p->save_file);

    // Call agents
    for (int i = 0; i < p->num_agents; i++) {
        agent_act(p->agents[i], p->history, p->history_count);
    }
}

// Self-scheduling callback
static void profiler_tick(void *raw, int) {
    Profiler *p = (Profiler *)raw;
    if (!p->active) return;

    profiler_profile(p);

    // Schedule next tick
    clock_schedule_delay(&p->base.collection->clock, p->period,
                         CONT(profiler_tick, p));
}

void profiler_run_periodically(Profiler *p, double period, double first_delay) {
    p->period = period;
    double delay = (first_delay > 0.0) ? first_delay : period;
    clock_schedule_delay(&p->base.collection->clock, delay,
                         CONT(profiler_tick, p));
}
