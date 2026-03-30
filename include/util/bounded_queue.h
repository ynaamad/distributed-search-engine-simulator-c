#ifndef DSE_BOUNDED_QUEUE_H
#define DSE_BOUNDED_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

// Generic bounded ring buffer queue storing void* items.
// Mirrors Python BoundedQueue with push_final / finalized semantics.
typedef struct {
    void **buf;
    int    head;
    int    tail;
    int    count;
    int    capacity;
    bool   finalized;
} BoundedQueue;

static inline void bq_init(BoundedQueue *q, int capacity) {
    q->capacity = capacity;
    q->buf = (void **)calloc((size_t)capacity, sizeof(void *));
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->finalized = false;
}

static inline void bq_free(BoundedQueue *q) {
    free(q->buf);
    q->buf = NULL;
    q->count = 0;
}

static inline bool bq_empty(const BoundedQueue *q) { return q->count == 0; }
static inline bool bq_full(const BoundedQueue *q) { return q->count >= q->capacity; }
static inline int  bq_size(const BoundedQueue *q) { return q->count; }

// Returns false if queue is full or finalized
static inline bool bq_push(BoundedQueue *q, void *item) {
    if (q->finalized || q->count >= q->capacity) return false;
    q->buf[q->tail] = item;
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    return true;
}

// Returns NULL if empty
static inline void *bq_pop(BoundedQueue *q) {
    if (q->count == 0) return NULL;
    void *item = q->buf[q->head];
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    return item;
}

// Mark queue as finalized (no more pushes allowed)
static inline void bq_finalize(BoundedQueue *q) {
    q->finalized = true;
}

// Iterate over all items currently in queue (non-destructive)
// Calls fn(item) for each item from head to tail
static inline void bq_foreach(BoundedQueue *q, void (*fn)(void *item, void *userdata), void *userdata) {
    int idx = q->head;
    for (int i = 0; i < q->count; i++) {
        fn(q->buf[idx], userdata);
        idx = (idx + 1) % q->capacity;
    }
}

#endif // DSE_BOUNDED_QUEUE_H
