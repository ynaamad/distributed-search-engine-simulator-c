#ifndef DSE_AGENT_H
#define DSE_AGENT_H

#include "collection/actor.h"
#include "event/continuation.h"
#include "types.h"
#include "profiler/profiler.h"

struct Agent {
    Actor          base;
    int            request_counts[ACTION_TYPE_COUNT];
    // vtable
    void         (*act)(Agent *self, ProfileEntry *history, int history_count);
};

void agent_init(Agent *a, DSECollection *coll,
                void (*act)(Agent *, ProfileEntry *, int));
void agent_run_profiler(Actor *self, ProfileResult *out);
void agent_act(Agent *a, ProfileEntry *history, int history_count);

// ─── ObliviousAgent ─────────────────────────────────────────────────────────

typedef struct {
    double     trigger_time;
    ActionType action;
    int        count;
    // For SCALE_TO:
    int        num_workers;
    int        half_ocus;
} ObliviousAction;

typedef struct {
    Agent            base_agent;
    ObliviousAction *actions;
    int              num_actions;
    int              actions_capacity;
} ObliviousAgent;

void oblivious_agent_init(ObliviousAgent *a, DSECollection *coll);
void oblivious_agent_add(ObliviousAgent *a, double time, ActionType action,
                         int count);
void oblivious_agent_add_scale_to(ObliviousAgent *a, double time,
                                   int num_workers, int half_ocus);
void oblivious_agent_free(ObliviousAgent *a);

// ─── SpendyAgent ────────────────────────────────────────────────────────────

typedef struct {
    Agent base_agent;
} SpendyAgent;

void spendy_agent_init(SpendyAgent *a, DSECollection *coll);

#endif // DSE_AGENT_H
