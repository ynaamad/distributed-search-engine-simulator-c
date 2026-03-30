#include "requests/request.h"
#include "collection/collection.h"
#include "collection/index.h"
#include "components/logical/shard.h"
#include "components/logical/worker.h"
#include "event/clock.h"
#include <stdlib.h>
#include <string.h>

static void put_phase_arrived(void *raw, int);
static void put_write_compute_done(void *raw, int result);
static void put_write_disk_done(void *raw, int result);
static void put_primary_done(void *raw, int result);
static void put_replica_done(void *raw, int result);

// Run compute + disk-write for one worker (used for primary and each replica)
static void put_start_write(PutWriteCtx *wctx, Continuation on_done) {
    worker_log_request(wctx->worker);
    shard_log_request(wctx->shard, wctx->worker);
    shard_log_compute(wctx->shard, wctx->worker, wctx->parent->cpu_size);
    shard_log_size(wctx->shard, wctx->worker, wctx->parent->mem_size);

    wctx->phase = 0;
    wctx->response = DSE_4XX;

    // Step 1: reserve_compute
    worker_reserve_compute(wctx->worker, wctx->parent->cpu_size,
                           wctx->parent->mem_size, on_done);
}

// Phase 0: schedule arrival
void put_request_start(PutRequestCtx *ctx) {
    ctx->phase = 0;
    clock_schedule(&ctx->coll->clock, ctx->time, CONT(put_phase_arrived, ctx));
}

// Phase 1: arrived — compute shard, start primary write
static void put_phase_arrived(void *raw, int) {
    PutRequestCtx *ctx = (PutRequestCtx *)raw;
    ctx->start_time = ctx->coll->clock.time;

    ctx->shard = index_compute_shard(ctx->index, ctx->document_id,
                                      ctx->given_hash, ctx->has_given_hash);

    // Start primary write
    PutWriteCtx *primary = (PutWriteCtx *)malloc(sizeof(PutWriteCtx));
    primary->parent = ctx;
    primary->shard = ctx->shard;
    primary->worker = shard_primary_worker(ctx->shard);
    put_start_write(primary, CONT(put_write_compute_done, primary));
}

// Compute done for this worker's write — proceed to disk
static void put_write_compute_done(void *raw, int result) {
    PutWriteCtx *wctx = (PutWriteCtx *)raw;
    if (dse_is_error(result)) {
        wctx->response = DSE_5XX;
    }

    // Step 2: reserve_disk (write)
    wctx->phase = 1;
    worker_reserve_disk(wctx->worker, DISK_WRITE, wctx->parent->mem_size,
                        CONT(put_write_disk_done, wctx));
}

// Disk write done — route to primary_done or replica_done
static void put_write_disk_done(void *raw, int result) {
    PutWriteCtx *wctx = (PutWriteCtx *)raw;
    if (dse_is_error(result)) {
        wctx->response = DSE_5XX;
    }

    int response = wctx->response;
    worker_log_result(wctx->worker, response);

    PutRequestCtx *ctx = wctx->parent;
    bool is_primary = (wctx->worker == shard_primary_worker(ctx->shard));
    free(wctx);

    if (is_primary) {
        put_primary_done(ctx, response);
    } else {
        put_replica_done(ctx, response);
    }
}

// Primary write done — if OK and replicas exist, fan out to replicas
static void put_primary_done(void *raw, int result) {
    PutRequestCtx *ctx = (PutRequestCtx *)raw;
    ctx->primary_response = result;

    if (dse_is_error(result) || shard_num_replicas(ctx->shard) == 0) {
        ctx->completion_time = ctx->coll->clock.time;
        index_log_result(ctx->index, result,
                         ctx->completion_time - ctx->start_time);
        free(ctx);
        return;
    }

    ctx->completion_time = ctx->coll->clock.time;

    // Start replica writes concurrently
    ctx->replica_count = shard_num_replicas(ctx->shard);
    ctx->replicas_done = 0;
    ctx->worst_response = DSE_4XX;

    for (int i = 1; i < ctx->shard->num_workers; i++) {
        PutWriteCtx *rep = (PutWriteCtx *)malloc(sizeof(PutWriteCtx));
        rep->parent = ctx;
        rep->shard = ctx->shard;
        rep->worker = ctx->shard->workers[i];
        put_start_write(rep, CONT(put_write_compute_done, rep));
    }
}

// One replica finished — when all replicas done, log final result
static void put_replica_done(void *raw, int result) {
    PutRequestCtx *ctx = (PutRequestCtx *)raw;
    if (result > ctx->worst_response) ctx->worst_response = result;
    ctx->replicas_done++;

    if (ctx->replicas_done == ctx->replica_count) {
        index_log_result(ctx->index, ctx->worst_response,
                         ctx->completion_time - ctx->start_time);
        free(ctx);
    }
}
