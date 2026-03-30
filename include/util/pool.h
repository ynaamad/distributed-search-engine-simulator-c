#ifndef DSE_POOL_H
#define DSE_POOL_H

#include <stddef.h>
#include <stdlib.h>

// Simple free-list pool allocator for fixed-size objects.
// Single-threaded, no locking. Falls back to malloc when empty.

typedef struct PoolFreeNode {
    struct PoolFreeNode *next;
} PoolFreeNode;

typedef struct {
    PoolFreeNode *free_list;
    size_t        obj_size;
} Pool;

static inline void pool_init(Pool *p, size_t obj_size) {
    p->free_list = NULL;
    p->obj_size = obj_size > sizeof(PoolFreeNode) ? obj_size : sizeof(PoolFreeNode);
}

static inline void *pool_alloc(Pool *p) {
    if (p->free_list) {
        PoolFreeNode *node = p->free_list;
        p->free_list = node->next;
        return (void *)node;
    }
    return malloc(p->obj_size);
}

static inline void pool_release(Pool *p, void *ptr) {
    PoolFreeNode *node = (PoolFreeNode *)ptr;
    node->next = p->free_list;
    p->free_list = node;
}

static inline void pool_free_all(Pool *p) {
    PoolFreeNode *node = p->free_list;
    while (node) {
        PoolFreeNode *next = node->next;
        free(node);
        node = next;
    }
    p->free_list = NULL;
}

#endif // DSE_POOL_H
