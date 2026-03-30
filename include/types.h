#ifndef DSE_TYPES_H
#define DSE_TYPES_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

// Result codes — ordered so max() aggregation works (5xx > 4xx)
typedef enum {
    DSE_OK = 0,
    DSE_4XX = 1,
    DSE_5XX = 2,
    DSE_ERR_QUEUE_FULL = 3,
    DSE_ERR_RAM_CAPACITY = 4,
    DSE_ERR_CPU_SHUTDOWN = 5,
    DSE_ERR_WORKER_SHUTDOWN = 6,
    DSE_ERR_NOT_STEADY = 7,
    DSE_ERR_INVALID_SCALING = 8,
    DSE_ERR_DISABLED_QUEUE = 9,
    DSE_ERR_INVALID_INDEX = 10,
} DseResult;

// Map any error to a 5xx response
static inline DseResult dse_result_to_response(DseResult r) {
    if (r == DSE_OK || r == DSE_4XX) return DSE_4XX;
    return DSE_5XX;
}

static inline bool dse_is_error(DseResult r) {
    return r >= DSE_5XX;
}

// Actor types
typedef enum {
    ACTOR_CPU,
    ACTOR_CPU_POOL,
    ACTOR_RAM,
    ACTOR_DISK,
    ACTOR_WORKER,
    ACTOR_SHARD,
    ACTOR_INDEX,
    ACTOR_PROFILER,
    ACTOR_AGENT,
    ACTOR_TYPE_COUNT
} ActorType;

// Worker status
typedef enum {
    WORKER_OBSOLETE = -1,
    WORKER_BLUE = 1,
    WORKER_GREEN = 2
} WorkerStatus;

// Disk operation type
typedef enum {
    DISK_READ,
    DISK_WRITE
} DiskOp;

// Request type
typedef enum {
    REQUEST_GET,
    REQUEST_PUT,
    REQUEST_SEARCH
} RequestType;

// Action type
typedef enum {
    ACTION_SCALE_UP,
    ACTION_SCALE_DOWN,
    ACTION_SCALE_OUT,
    ACTION_SCALE_IN,
    ACTION_SCALE_TO,
    ACTION_SHUT_DOWN,
    ACTION_TYPE_COUNT
} ActionType;

// Forward declarations
typedef struct DSECollection DSECollection;
typedef struct Actor Actor;
typedef struct CPU CPU;
typedef struct CPUPool CPUPool;
typedef struct RAM RAM;
typedef struct Disk Disk;
typedef struct Worker Worker;
typedef struct Shard Shard;
typedef struct Index Index;
typedef struct Profiler Profiler;
typedef struct Agent Agent;
typedef struct Clock Clock;

#endif // DSE_TYPES_H
