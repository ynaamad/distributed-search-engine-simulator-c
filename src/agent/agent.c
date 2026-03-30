#include "agent/agent.h"
#include "collection/collection.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

void agent_run_profiler(Actor *self, ProfileResult *out) {
    Agent *a = (Agent *)self;
    out->count = 0;
    for (int i = 0; i < ACTION_TYPE_COUNT; i++) {
        if (a->request_counts[i] > 0) {
            static const char *action_names[] = {
                "ScaleUp", "ScaleDown", "ScaleOut", "ScaleIn", "ScaleTo", "ShutDown"
            };
            strncpy(out->entries[out->count].key, action_names[i],
                    sizeof(out->entries[0].key));
            out->entries[out->count].value = (double)a->request_counts[i];
            out->count++;
            a->request_counts[i] = 0;
        }
    }
}

void agent_init(Agent *a, DSECollection *coll,
                void (*act)(Agent *, ProfileEntry *, int)) {
    actor_init(&a->base, ACTOR_AGENT, coll, agent_run_profiler);
    memset(a->request_counts, 0, sizeof(a->request_counts));
    a->act = act;
}

void agent_act(Agent *a, ProfileEntry *history, int history_count) {
    if (a->act) a->act(a, history, history_count);
}

// ─── ObliviousAgent ─────────────────────────────────────────────────────────

static void oblivious_act(Agent *self, ProfileEntry *, int) {
    ObliviousAgent *a = (ObliviousAgent *)self;
    double current_time = self->base.collection->clock.time;

    for (int i = 0; i < a->num_actions; ) {
        if (current_time >= a->actions[i].trigger_time) {
            ObliviousAction *act = &a->actions[i];
            self->request_counts[act->action]++;

            switch (act->action) {
                case ACTION_SCALE_UP:
                    collection_scale_up(self->base.collection, act->count, CONT_NULL);
                    break;
                case ACTION_SCALE_DOWN:
                    collection_scale_down(self->base.collection, act->count, CONT_NULL);
                    break;
                case ACTION_SCALE_OUT:
                    collection_scale_out(self->base.collection, act->count, CONT_NULL);
                    break;
                case ACTION_SCALE_IN:
                    collection_scale_in(self->base.collection, act->count, CONT_NULL);
                    break;
                case ACTION_SCALE_TO:
                    collection_scale_to(self->base.collection,
                                        act->num_workers, act->half_ocus, CONT_NULL);
                    break;
                case ACTION_SHUT_DOWN:
                    clock_stop(&self->base.collection->clock);
                    break;
                default:
                    break;
            }

            // Remove this action (swap with last)
            a->actions[i] = a->actions[--a->num_actions];
        } else {
            i++;
        }
    }
}

void oblivious_agent_init(ObliviousAgent *a, DSECollection *coll) {
    agent_init(&a->base_agent, coll, oblivious_act);
    a->num_actions = 0;
    a->actions_capacity = 32;
    a->actions = (ObliviousAction *)malloc(a->actions_capacity * sizeof(ObliviousAction));
}

void oblivious_agent_add(ObliviousAgent *a, double time, ActionType action, int count) {
    if (a->num_actions == a->actions_capacity) {
        a->actions_capacity *= 2;
        a->actions = (ObliviousAction *)realloc(a->actions,
            a->actions_capacity * sizeof(ObliviousAction));
    }
    a->actions[a->num_actions++] = (ObliviousAction){
        .trigger_time = time,
        .action = action,
        .count = count,
    };
}

void oblivious_agent_add_scale_to(ObliviousAgent *a, double time,
                                   int num_workers, int half_ocus) {
    if (a->num_actions == a->actions_capacity) {
        a->actions_capacity *= 2;
        a->actions = (ObliviousAction *)realloc(a->actions,
            a->actions_capacity * sizeof(ObliviousAction));
    }
    a->actions[a->num_actions++] = (ObliviousAction){
        .trigger_time = time,
        .action = ACTION_SCALE_TO,
        .num_workers = num_workers,
        .half_ocus = half_ocus,
    };
}

void oblivious_agent_free(ObliviousAgent *a) {
    free(a->actions);
    a->actions = NULL;
}

// ─── SpendyAgent ────────────────────────────────────────────────────────────

static void spendy_act(Agent *self, ProfileEntry *history, int count) {
    if (count == 0) return;

    double last_time = history[count - 1].time;
    int time_min = (int)(last_time / 60.0);
    if (time_min % 5 != 0) return;

    // Compute mean CPU utilization at latest time
    double sum = 0.0;
    int n = 0;
    for (int i = count - 1; i >= 0 && history[i].time == last_time; i--) {
        if (strstr(history[i].actor, "CPU") &&
            strstr(history[i].key, "utilization_frac")) {
            sum += history[i].value;
            n++;
        }
    }

    if (n > 0 && (sum / n) >= 0.5) {
        self->request_counts[ACTION_SCALE_UP]++;
        collection_scale_up(self->base.collection, 1, CONT_NULL);
    }
}

void spendy_agent_init(SpendyAgent *a, DSECollection *coll) {
    agent_init(&a->base_agent, coll, spendy_act);
}
