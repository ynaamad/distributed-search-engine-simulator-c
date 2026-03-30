#ifndef DSE_CONTINUATION_H
#define DSE_CONTINUATION_H

#include "types.h"
#include <stdlib.h>

// A continuation is a callback + context pointer.
// When an async operation completes, the callback is invoked with
// the context and a result code.
typedef void (*ContCallback)(void *ctx, int result);

typedef struct {
    ContCallback callback;
    void        *ctx;
} Continuation;

#define CONT(fn, context) ((Continuation){.callback = (ContCallback)(fn), .ctx = (context)})
#define CONT_NULL          ((Continuation){.callback = NULL, .ctx = NULL})

static inline void cont_invoke(Continuation c, int result) {
    if (c.callback) c.callback(c.ctx, result);
}

static inline bool cont_is_null(Continuation c) {
    return c.callback == NULL;
}

// GatherCtx: fan-out/fan-in for concurrent operations.
// Launch N children, each calls gather_child_done when complete.
// When all children finish, the parent continuation is invoked.
typedef struct GatherCtx {
    Continuation parent;
    int          total;
    int          completed;
    int          worst_result;
} GatherCtx;

static inline GatherCtx *gather_create(int total, Continuation parent) {
    GatherCtx *g = (GatherCtx *)malloc(sizeof(GatherCtx));
    g->parent = parent;
    g->total = total;
    g->completed = 0;
    g->worst_result = DSE_OK;
    return g;
}

// Called by each child when it finishes. Frees gather and invokes parent
// when all children are done.
static inline void gather_child_done(void *raw, int result) {
    GatherCtx *g = (GatherCtx *)raw;
    if (result > g->worst_result) g->worst_result = result;
    g->completed++;
    if (g->completed == g->total) {
        int worst = g->worst_result;
        Continuation parent = g->parent;
        free(g);
        cont_invoke(parent, worst);
    }
}

#endif // DSE_CONTINUATION_H
