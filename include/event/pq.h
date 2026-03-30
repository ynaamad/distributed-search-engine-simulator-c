#ifndef DSE_PQ_H
#define DSE_PQ_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Forward declare — Continuation is defined in continuation.h
// but pq.h must not depend on it to avoid circular includes.
// We store the callback + ctx inline.
typedef void (*ContCallback)(void *ctx, int result);

typedef struct {
    ContCallback callback;
    void        *ctx;
} PQCont;

typedef struct {
    double   time;
    uint64_t seq;
    PQCont   cont;
} PQEntry;

typedef struct {
    PQEntry *entries;
    size_t   size;
    size_t   capacity;
} PriorityQueue;

void pq_init(PriorityQueue *pq, size_t initial_capacity);
void pq_free(PriorityQueue *pq);
void pq_push(PriorityQueue *pq, double time, uint64_t seq, PQCont cont);
PQEntry pq_pop(PriorityQueue *pq);
PQEntry pq_peek(const PriorityQueue *pq);

static inline size_t pq_size(const PriorityQueue *pq) { return pq->size; }
static inline bool pq_empty(const PriorityQueue *pq) { return pq->size == 0; }

#endif // DSE_PQ_H
