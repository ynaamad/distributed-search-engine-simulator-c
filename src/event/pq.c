#include "event/pq.h"
#include <stdlib.h>
#include <assert.h>

void pq_init(PriorityQueue *pq, size_t initial_capacity) {
    pq->size = 0;
    pq->capacity = initial_capacity > 0 ? initial_capacity : 4096;
    pq->entries = (PQEntry *)malloc(pq->capacity * sizeof(PQEntry));
}

void pq_free(PriorityQueue *pq) {
    free(pq->entries);
    pq->entries = NULL;
    pq->size = 0;
    pq->capacity = 0;
}

static inline bool pq_less(const PQEntry *a, const PQEntry *b) {
    if (a->time != b->time) return a->time < b->time;
    return a->seq < b->seq;
}

static inline void pq_swap(PQEntry *a, PQEntry *b) {
    PQEntry tmp = *a;
    *a = *b;
    *b = tmp;
}

static void pq_sift_up(PriorityQueue *pq, size_t i) {
    while (i > 0) {
        size_t parent = (i - 1) / 2;
        if (pq_less(&pq->entries[i], &pq->entries[parent])) {
            pq_swap(&pq->entries[i], &pq->entries[parent]);
            i = parent;
        } else {
            break;
        }
    }
}

static void pq_sift_down(PriorityQueue *pq, size_t i) {
    size_t n = pq->size;
    while (true) {
        size_t smallest = i;
        size_t left = 2 * i + 1;
        size_t right = 2 * i + 2;

        if (left < n && pq_less(&pq->entries[left], &pq->entries[smallest]))
            smallest = left;
        if (right < n && pq_less(&pq->entries[right], &pq->entries[smallest]))
            smallest = right;

        if (smallest != i) {
            pq_swap(&pq->entries[i], &pq->entries[smallest]);
            i = smallest;
        } else {
            break;
        }
    }
}

void pq_push(PriorityQueue *pq, double time, uint64_t seq, PQCont cont) {
    if (pq->size == pq->capacity) {
        pq->capacity *= 2;
        pq->entries = (PQEntry *)realloc(pq->entries, pq->capacity * sizeof(PQEntry));
    }
    pq->entries[pq->size] = (PQEntry){.time = time, .seq = seq, .cont = cont};
    pq_sift_up(pq, pq->size);
    pq->size++;
}

PQEntry pq_pop(PriorityQueue *pq) {
    assert(pq->size > 0);
    PQEntry top = pq->entries[0];
    pq->size--;
    if (pq->size > 0) {
        pq->entries[0] = pq->entries[pq->size];
        pq_sift_down(pq, 0);
    }
    return top;
}

PQEntry pq_peek(const PriorityQueue *pq) {
    assert(pq->size > 0);
    return pq->entries[0];
}
