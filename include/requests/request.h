#ifndef DSE_REQUEST_H
#define DSE_REQUEST_H

#include "types.h"
#include "event/continuation.h"

// ─── GET Request ────────────────────────────────────────────────────────────

typedef struct {
    DSECollection *coll;
    double         time;
    Index         *index;
    int            document_id;
    double         cpu_size;
    double         mem_size;
    int            given_hash;
    bool           has_given_hash;
    // State
    int            phase;
    Shard         *shard;
    Worker        *worker;
    double         start_time;
    double         completion_time;
} GetRequestCtx;

void get_request_start(GetRequestCtx *ctx);

// ─── PUT Request ────────────────────────────────────────────────────────────

typedef struct PutRequestCtx PutRequestCtx;

// Per-worker write context (used for primary + each replica)
typedef struct {
    PutRequestCtx *parent;
    Shard         *shard;
    Worker        *worker;
    int            phase;
    int            response;
} PutWriteCtx;

struct PutRequestCtx {
    DSECollection *coll;
    double         time;
    Index         *index;
    int            document_id;
    double         cpu_size;
    double         mem_size;
    int            given_hash;
    bool           has_given_hash;
    // State
    int            phase;
    Shard         *shard;
    double         start_time;
    double         completion_time;
    int            primary_response;
    // Replica gather
    int            replica_count;
    int            replicas_done;
    int            worst_response;
};

void put_request_start(PutRequestCtx *ctx);

// ─── SEARCH Request ─────────────────────────────────────────────────────────

typedef struct SearchRequestCtx SearchRequestCtx;

// Per-shard search context
typedef struct {
    SearchRequestCtx *parent;
    Shard            *shard;
    Worker           *worker;
    int               phase;
    int               response;
} SearchShardCtx;

struct SearchRequestCtx {
    DSECollection *coll;
    double         time;
    Index         *index;
    double         cpu_size_query;
    double         mem_size_query;
    double         cpu_size_response;
    double         mem_size_response;
    double         cpu_size_collate;
    int           *given_hashes;
    int            num_given_hashes;
    // State
    int            phase;
    double         start_time;
    double         completion_time;
    // Shard fan-out
    Shard        **shards;
    Worker       **workers;
    int            num_shards;
    int            shards_done;
    int            worst_response;
};

void search_request_start(SearchRequestCtx *ctx);

#endif // DSE_REQUEST_H
