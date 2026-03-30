#include "requests/request.h"
#include "collection/collection.h"
#include "collection/index.h"
#include "components/logical/shard.h"
#include "components/logical/worker.h"
#include "event/clock.h"
#include <stdlib.h>

static void get_phase_arrived(void *raw, int);
static void get_phase_compute_done(void *raw, int result);
static void get_phase_disk_done(void *raw, int result);

// Phase 0: schedule arrival
void get_request_start(GetRequestCtx *ctx) {
    ctx->phase = 0;
    clock_schedule(&ctx->coll->clock, ctx->time, CONT(get_phase_arrived, ctx));
}

// Phase 1: arrived — compute shard, pick worker, start reserve_compute
static void get_phase_arrived(void *raw, int) {
    GetRequestCtx *ctx = (GetRequestCtx *)raw;
    ctx->start_time = ctx->coll->clock.time;

    ctx->shard = index_compute_shard(ctx->index, ctx->document_id,
                                      ctx->given_hash, ctx->has_given_hash);
    ctx->worker = shard_any_worker(ctx->shard);

    shard_log_request(ctx->shard, ctx->worker);
    shard_log_compute(ctx->shard, ctx->worker, ctx->cpu_size);
    worker_log_request(ctx->worker);

    ctx->phase = 1;
    worker_reserve_compute(ctx->worker, ctx->cpu_size, ctx->mem_size,
                           CONT(get_phase_compute_done, ctx));
}

// Phase 2: compute done — check result, start disk read
static void get_phase_compute_done(void *raw, int result) {
    GetRequestCtx *ctx = (GetRequestCtx *)raw;

    if (dse_is_error(result)) {
        ctx->completion_time = ctx->coll->clock.time;
        worker_log_result(ctx->worker, DSE_5XX);
        index_log_result(ctx->index, DSE_5XX,
                         ctx->completion_time - ctx->start_time);
        free(ctx);
        return;
    }

    ctx->phase = 2;
    worker_reserve_disk(ctx->worker, DISK_READ, ctx->mem_size,
                        CONT(get_phase_disk_done, ctx));
}

// Phase 3: disk done — log result, free
static void get_phase_disk_done(void *raw, int result) {
    GetRequestCtx *ctx = (GetRequestCtx *)raw;
    ctx->completion_time = ctx->coll->clock.time;

    int response = dse_is_error(result) ? DSE_5XX : DSE_4XX;
    worker_log_result(ctx->worker, response);
    index_log_result(ctx->index, response,
                     ctx->completion_time - ctx->start_time);
    free(ctx);
}
