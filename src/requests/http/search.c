#include "requests/request.h"
#include "collection/collection.h"
#include "collection/index.h"
#include "components/logical/shard.h"
#include "components/logical/worker.h"
#include "event/clock.h"
#include "util/rng.h"
#include <stdlib.h>
#include <string.h>

static void search_phase_arrived(void *raw, int);
static void search_shard_compute_done(void *raw, int result);
static void search_shard_disk_done(void *raw, int result);
static void search_shard_response_done(void *raw, int result);
static void search_shard_finished(SearchRequestCtx *ctx, int shard_response);
static void search_collate_done(void *raw, int result);

// Schedule a SEARCH request: fan out to all shards, then collate results
void search_request_start(SearchRequestCtx *ctx) {
    ctx->phase = 0;
    clock_schedule(&ctx->coll->clock, ctx->time, CONT(search_phase_arrived, ctx));
}

// Resolve shards, pick workers, launch per-shard queries concurrently
static void search_phase_arrived(void *raw, int) {
    SearchRequestCtx *ctx = (SearchRequestCtx *)raw;
    ctx->start_time = ctx->coll->clock.time;

    Index *idx = ctx->index;

    // Identify relevant shards
    if (ctx->num_given_hashes == 0) {
        // All shards
        ctx->num_shards = (int)idx->shards.size;
        ctx->shards = (Shard **)malloc(ctx->num_shards * sizeof(Shard *));
        for (int i = 0; i < ctx->num_shards; i++) {
            ctx->shards[i] = (Shard *)idx->shards.items[i];
        }
    } else {
        // Deduplicate shards from given hashes
        Shard **temp = (Shard **)malloc(ctx->num_given_hashes * sizeof(Shard *));
        int n = 0;
        for (int i = 0; i < ctx->num_given_hashes; i++) {
            Shard *s = index_get_shard_from_hash(idx, ctx->given_hashes[i]);
            // Check for duplicates
            bool found = false;
            for (int j = 0; j < n; j++) {
                if (temp[j] == s) { found = true; break; }
            }
            if (!found) temp[n++] = s;
        }
        ctx->shards = temp;
        ctx->num_shards = n;
    }

    // Worker cover: pick a random worker per shard
    ctx->workers = (Worker **)malloc(ctx->num_shards * sizeof(Worker *));
    for (int i = 0; i < ctx->num_shards; i++) {
        ctx->workers[i] = shard_any_worker(ctx->shards[i]);
    }

    ctx->shards_done = 0;
    ctx->worst_response = DSE_4XX;

    if (ctx->num_shards == 0) {
        // Edge case: no shards
        ctx->completion_time = ctx->coll->clock.time;
        index_log_result(ctx->index, DSE_4XX, ctx->completion_time - ctx->start_time);
        free(ctx->shards);
        free(ctx->workers);
        if (ctx->given_hashes) free(ctx->given_hashes);
        free(ctx);
        return;
    }

    // Launch per-shard searches concurrently
    for (int i = 0; i < ctx->num_shards; i++) {
        SearchShardCtx *sc = (SearchShardCtx *)malloc(sizeof(SearchShardCtx));
        sc->parent = ctx;
        sc->shard = ctx->shards[i];
        sc->worker = ctx->workers[i];
        sc->phase = 0;
        sc->response = DSE_4XX;

        worker_log_request(sc->worker);
        shard_log_request(sc->shard, sc->worker);
        shard_log_compute(sc->shard, sc->worker,
                          ctx->cpu_size_query + ctx->cpu_size_response);

        // Step 1: query compute
        worker_reserve_compute(sc->worker, ctx->cpu_size_query, ctx->mem_size_query,
                               CONT(search_shard_compute_done, sc));
    }
}

// Per-shard: query compute done, now read from disk
static void search_shard_compute_done(void *raw, int result) {
    SearchShardCtx *sc = (SearchShardCtx *)raw;
    if (dse_is_error(result)) sc->response = DSE_5XX;

    // Step 2: disk read
    sc->phase = 1;
    worker_reserve_disk(sc->worker, DISK_READ, sc->parent->mem_size_response,
                        CONT(search_shard_disk_done, sc));
}

// Per-shard: disk read done, now compute the response
static void search_shard_disk_done(void *raw, int result) {
    SearchShardCtx *sc = (SearchShardCtx *)raw;
    if (dse_is_error(result)) sc->response = DSE_5XX;

    // Step 3: response compute
    sc->phase = 2;
    worker_reserve_compute(sc->worker, sc->parent->cpu_size_response,
                           sc->parent->mem_size_response,
                           CONT(search_shard_response_done, sc));
}

// Per-shard: all 3 steps done, report to parent gather
static void search_shard_response_done(void *raw, int result) {
    SearchShardCtx *sc = (SearchShardCtx *)raw;
    if (dse_is_error(result)) sc->response = DSE_5XX;

    int response = sc->response;
    worker_log_result(sc->worker, response);
    SearchRequestCtx *ctx = sc->parent;
    free(sc);

    search_shard_finished(ctx, response);
}

// Gather: count completed shards; when all done, pick a worker and collate
static void search_shard_finished(SearchRequestCtx *ctx, int shard_response) {
    if (shard_response > ctx->worst_response)
        ctx->worst_response = shard_response;
    ctx->shards_done++;

    if (ctx->shards_done < ctx->num_shards) return;

    // All shards done
    if (dse_is_error(ctx->worst_response)) {
        ctx->completion_time = ctx->coll->clock.time;
        index_log_result(ctx->index, ctx->worst_response,
                         ctx->completion_time - ctx->start_time);
        free(ctx->shards);
        free(ctx->workers);
        if (ctx->given_hashes) free(ctx->given_hashes);
        free(ctx);
        return;
    }

    // Collation: pick worker with most shards, random tiebreak
    int best_idx = 0;
    int best_count = 0;
    for (int i = 0; i < ctx->num_shards; i++) {
        int count = 0;
        for (int j = 0; j < ctx->num_shards; j++) {
            if (ctx->workers[j] == ctx->workers[i]) count++;
        }
        if (count > best_count ||
            (count == best_count &&
             rng_double(&ctx->coll->rng) > 0.5)) {
            best_count = count;
            best_idx = i;
        }
    }

    Worker *collate_worker = ctx->workers[best_idx];
    double collate_mem = (double)ctx->num_shards * ctx->mem_size_response;

    worker_reserve_compute(collate_worker, ctx->cpu_size_collate, collate_mem,
                           CONT(search_collate_done, ctx));
}

// Collation compute finished — record final latency and clean up
static void search_collate_done(void *raw, int result) {
    SearchRequestCtx *ctx = (SearchRequestCtx *)raw;
    ctx->completion_time = ctx->coll->clock.time;

    int response = dse_is_error(result) ? DSE_5XX : DSE_4XX;
    index_log_result(ctx->index, response,
                     ctx->completion_time - ctx->start_time);

    free(ctx->shards);
    free(ctx->workers);
    if (ctx->given_hashes) free(ctx->given_hashes);
    free(ctx);
}
