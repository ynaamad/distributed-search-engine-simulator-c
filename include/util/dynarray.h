#ifndef DSE_DYNARRAY_H
#define DSE_DYNARRAY_H

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void **items;
    size_t size;
    size_t capacity;
} DynArray;

static inline void dynarray_init(DynArray *a, size_t initial_capacity) {
    a->size = 0;
    a->capacity = initial_capacity > 0 ? initial_capacity : 8;
    a->items = (void **)malloc(a->capacity * sizeof(void *));
}

static inline void dynarray_free(DynArray *a) {
    free(a->items);
    a->items = NULL;
    a->size = 0;
    a->capacity = 0;
}

static inline void dynarray_push(DynArray *a, void *item) {
    if (a->size == a->capacity) {
        a->capacity *= 2;
        a->items = (void **)realloc(a->items, a->capacity * sizeof(void *));
    }
    a->items[a->size++] = item;
}

static inline void *dynarray_pop(DynArray *a) {
    if (a->size == 0) return NULL;
    return a->items[--a->size];
}

static inline void *dynarray_get(const DynArray *a, size_t i) {
    return a->items[i];
}

static inline bool dynarray_remove(DynArray *a, void *item) {
    for (size_t i = 0; i < a->size; i++) {
        if (a->items[i] == item) {
            a->items[i] = a->items[--a->size];
            return true;
        }
    }
    return false;
}

static inline bool dynarray_remove_ordered(DynArray *a, void *item) {
    for (size_t i = 0; i < a->size; i++) {
        if (a->items[i] == item) {
            memmove(&a->items[i], &a->items[i + 1], (a->size - i - 1) * sizeof(void *));
            a->size--;
            return true;
        }
    }
    return false;
}

static inline bool dynarray_contains(const DynArray *a, void *item) {
    for (size_t i = 0; i < a->size; i++) {
        if (a->items[i] == item) return true;
    }
    return false;
}

#endif // DSE_DYNARRAY_H
