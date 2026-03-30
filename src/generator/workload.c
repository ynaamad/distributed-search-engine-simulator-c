#include "generator/workload.h"
#include "collection/collection.h"
#include "collection/index.h"
#include "components/logical/shard.h"
#include "components/logical/worker.h"
#include "requests/request.h"
#include "event/clock.h"
#include "util/rng.h"
#include "util/json_scan.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

// ─── JSONL Loader ───────────────────────────────────────────────────────────

// Single-pass callback: match key and parse value into the RequestDescriptor
static void parse_request_field(const char *key, int key_len,
                                const char *val, void *userdata) {
    RequestDescriptor *rd = (RequestDescriptor *)userdata;

    // Use first char + length for fast key dispatch
    switch (key[0]) {
    case 't':
        if (key_len == 4 && memcmp(key, "type", 4) == 0) {
            char buf[8];
            json_parse_string(val, buf, sizeof(buf));
            if (buf[0] == 'G') rd->type = REQUEST_GET;
            else if (buf[0] == 'P') rd->type = REQUEST_PUT;
            else if (buf[0] == 'S') rd->type = REQUEST_SEARCH;
        } else if (key_len == 4 && memcmp(key, "time", 4) == 0) {
            rd->time = json_parse_number(val);
        }
        break;
    case 'i':
        if (key_len == 5 && memcmp(key, "index", 5) == 0)
            json_parse_string(val, rd->index_name, sizeof(rd->index_name));
        break;
    case 'd':
        if (key_len == 11 && memcmp(key, "document_id", 11) == 0)
            rd->document_id = (int)json_parse_number(val);
        break;
    case 'c':
        if (key_len == 8 && memcmp(key, "cpu_size", 8) == 0)
            rd->cpu_size = json_parse_number(val);
        else if (key_len == 14 && memcmp(key, "cpu_size_query", 14) == 0)
            rd->cpu_size_query = json_parse_number(val);
        else if (key_len == 17 && memcmp(key, "cpu_size_response", 17) == 0)
            rd->cpu_size_response = json_parse_number(val);
        else if (key_len == 16 && memcmp(key, "cpu_size_collate", 16) == 0)
            rd->cpu_size_collate = json_parse_number(val);
        break;
    case 'm':
        if (key_len == 8 && memcmp(key, "mem_size", 8) == 0)
            rd->mem_size = json_parse_number(val);
        else if (key_len == 14 && memcmp(key, "mem_size_query", 14) == 0)
            rd->mem_size_query = json_parse_number(val);
        else if (key_len == 17 && memcmp(key, "mem_size_response", 17) == 0)
            rd->mem_size_response = json_parse_number(val);
        break;
    case 'g':
        if (key_len == 10 && memcmp(key, "given_hash", 10) == 0) {
            if (!json_is_empty_array(val)) {
                int gh;
                if (json_parse_int_array(val, &gh, 1) > 0) {
                    rd->given_hash = gh;
                    rd->has_given_hash = true;
                }
            }
        } else if (key_len == 12 && memcmp(key, "given_hashes", 12) == 0) {
            if (!json_is_empty_array(val)) {
                int temp[256];
                int n = json_parse_int_array(val, temp, 256);
                if (n > 0) {
                    rd->given_hashes = (int *)malloc(n * sizeof(int));
                    memcpy(rd->given_hashes, temp, n * sizeof(int));
                    rd->num_given_hashes = n;
                }
            }
        }
        break;
    }
}

int workload_load(LoadedWorkloadGenerator *gen, DSECollection *coll,
                  const char *path) {
    gen->coll = coll;
    gen->current = 0;
    gen->active = true;

    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open workload file: %s\n", path);
        return -1;
    }

    // Count lines first
    int capacity = 1024;
    gen->requests = (RequestDescriptor *)malloc(capacity * sizeof(RequestDescriptor));
    gen->num_requests = 0;

    char line[4096];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '\0') continue;

        if (gen->num_requests == capacity) {
            capacity *= 2;
            gen->requests = (RequestDescriptor *)realloc(gen->requests,
                capacity * sizeof(RequestDescriptor));
        }

        RequestDescriptor *rd = &gen->requests[gen->num_requests];
        memset(rd, 0, sizeof(*rd));

        // Single-pass: walk the JSON line once, dispatch each key
        json_scan_object(line, parse_request_field, rd);

        if (rd->type == REQUEST_GET || rd->type == REQUEST_PUT || rd->type == REQUEST_SEARCH)
            gen->num_requests++;
    }

    fclose(f);
    return gen->num_requests;
}

// Dispatch the next request
static void workload_dispatch_next(void *raw, int);

static void dispatch_request(LoadedWorkloadGenerator *gen, RequestDescriptor *rd) {
    Index *idx = collection_get_index(gen->coll, rd->index_name);
    if (!idx) {
        fprintf(stderr, "Warning: unknown index '%s', skipping request\n", rd->index_name);
        return;
    }

    switch (rd->type) {
        case REQUEST_GET: {
            GetRequestCtx *ctx = (GetRequestCtx *)calloc(1, sizeof(GetRequestCtx));
            ctx->coll = gen->coll;
            ctx->time = rd->time;
            ctx->index = idx;
            ctx->document_id = rd->document_id;
            ctx->cpu_size = rd->cpu_size;
            ctx->mem_size = rd->mem_size;
            ctx->given_hash = rd->given_hash;
            ctx->has_given_hash = rd->has_given_hash;
            get_request_start(ctx);
            break;
        }
        case REQUEST_PUT: {
            PutRequestCtx *ctx = (PutRequestCtx *)calloc(1, sizeof(PutRequestCtx));
            ctx->coll = gen->coll;
            ctx->time = rd->time;
            ctx->index = idx;
            ctx->document_id = rd->document_id;
            ctx->cpu_size = rd->cpu_size;
            ctx->mem_size = rd->mem_size;
            ctx->given_hash = rd->given_hash;
            ctx->has_given_hash = rd->has_given_hash;
            put_request_start(ctx);
            break;
        }
        case REQUEST_SEARCH: {
            SearchRequestCtx *ctx = (SearchRequestCtx *)calloc(1, sizeof(SearchRequestCtx));
            ctx->coll = gen->coll;
            ctx->time = rd->time;
            ctx->index = idx;
            ctx->cpu_size_query = rd->cpu_size_query;
            ctx->mem_size_query = rd->mem_size_query;
            ctx->cpu_size_response = rd->cpu_size_response;
            ctx->mem_size_response = rd->mem_size_response;
            ctx->cpu_size_collate = rd->cpu_size_collate;
            if (rd->num_given_hashes > 0) {
                ctx->given_hashes = (int *)malloc(rd->num_given_hashes * sizeof(int));
                memcpy(ctx->given_hashes, rd->given_hashes,
                       rd->num_given_hashes * sizeof(int));
                ctx->num_given_hashes = rd->num_given_hashes;
            }
            search_request_start(ctx);
            break;
        }
    }
}

static void workload_stop_clock(void *raw, int) {
    LoadedWorkloadGenerator *gen = (LoadedWorkloadGenerator *)raw;
    clock_stop(&gen->coll->clock);
}

static void workload_dispatch_next(void *raw, int) {
    LoadedWorkloadGenerator *gen = (LoadedWorkloadGenerator *)raw;

    if (!gen->active || gen->current >= gen->num_requests) {
        return;
    }

    RequestDescriptor *rd = &gen->requests[gen->current++];
    dispatch_request(gen, rd);

    if (gen->current < gen->num_requests) {
        double next_time = gen->requests[gen->current].time;
        clock_schedule(&gen->coll->clock, next_time,
                       CONT(workload_dispatch_next, gen));
    } else {
        // All requests dispatched — schedule stop after they complete.
        // 1s is plenty of headroom for any in-flight request to finish.
        gen->active = false;
        clock_schedule_delay(&gen->coll->clock, 1.0,
                             CONT(workload_stop_clock, gen));
    }
}

void workload_start(LoadedWorkloadGenerator *gen) {
    if (gen->num_requests == 0) return;

    // Schedule first request
    double first_time = gen->requests[0].time;
    clock_schedule(&gen->coll->clock, first_time,
                   CONT(workload_dispatch_next, gen));
}

void workload_free(LoadedWorkloadGenerator *gen) {
    for (int i = 0; i < gen->num_requests; i++) {
        if (gen->requests[i].given_hashes) {
            free(gen->requests[i].given_hashes);
        }
    }
    free(gen->requests);
    gen->requests = NULL;
}

// ─── Poisson Generator ─────────────────────────────────────────────────────

static void poisson_dispatch_next(void *raw, int);

void poisson_workload_init(PoissonWorkloadGenerator *gen, DSECollection *coll,
                           double rate, double end_time,
                           double cpu_mean, double cpu_std,
                           double mem_mean, double mem_std,
                           const char *save_path) {
    gen->coll = coll;
    gen->rng = &coll->rng;
    gen->rate = rate;
    gen->sinusoidal = false;
    gen->cpu_mean = cpu_mean;
    gen->cpu_std = cpu_std;
    gen->mem_mean = mem_mean;
    gen->mem_std = mem_std;
    gen->count = 0;
    gen->end_time = end_time;
    gen->active = true;

    if (save_path) {
        gen->save_file = fopen(save_path, "w");
    } else {
        gen->save_file = NULL;
    }
}

void poisson_workload_init_sinusoidal(PoissonWorkloadGenerator *gen,
                                      DSECollection *coll,
                                      double rate_mean, double rate_period,
                                      double end_time,
                                      double cpu_mean, double cpu_std,
                                      double mem_mean, double mem_std,
                                      const char *save_path) {
    poisson_workload_init(gen, coll, rate_mean, end_time,
                          cpu_mean, cpu_std, mem_mean, mem_std, save_path);
    gen->sinusoidal = true;
    gen->rate_mean = rate_mean;
    gen->rate_period = rate_period;
}

static double poisson_get_rate(PoissonWorkloadGenerator *gen, double time) {
    if (!gen->sinusoidal) return gen->rate;
    double c1 = (1.0 + sin(2.0 * M_PI * time / gen->rate_period)) / 2.0;
    return (0.5 + 1.5 * c1) * gen->rate_mean;
}

static void poisson_dispatch_next(void *raw, int) {
    PoissonWorkloadGenerator *gen = (PoissonWorkloadGenerator *)raw;
    DSECollection *coll = gen->coll;

    if (!gen->active || coll->clock.time >= gen->end_time) {
        clock_stop(&coll->clock);
        return;
    }

    gen->count++;

    // Generate request parameters
    double cpu_size = fmax(1.0, rng_gauss(gen->rng, gen->cpu_mean, gen->cpu_std));
    double mem_size = fmax(1.0, rng_gauss(gen->rng, gen->mem_mean, gen->mem_std));

    // Pick random type (35% GET, 50% PUT, 15% SEARCH)
    double rnd = rng_uniform(gen->rng, 0.0, 1.0);

    // Pick random index
    if (coll->num_indices == 0) return;
    int idx_i = rng_choice_index(gen->rng, coll->num_indices);
    Index *idx = coll->index_entries[idx_i].index;
    int doc_id = rng_int(gen->rng, 0, 1000000000);

    if (rnd <= 0.35) {
        GetRequestCtx *ctx = (GetRequestCtx *)calloc(1, sizeof(GetRequestCtx));
        ctx->coll = coll;
        ctx->time = coll->clock.time;
        ctx->index = idx;
        ctx->document_id = doc_id;
        ctx->cpu_size = cpu_size;
        ctx->mem_size = mem_size;
        get_request_start(ctx);
    } else if (rnd <= 0.85) {
        PutRequestCtx *ctx = (PutRequestCtx *)calloc(1, sizeof(PutRequestCtx));
        ctx->coll = coll;
        ctx->time = coll->clock.time;
        ctx->index = idx;
        ctx->document_id = doc_id;
        ctx->cpu_size = cpu_size;
        ctx->mem_size = mem_size;
        put_request_start(ctx);
    } else {
        SearchRequestCtx *ctx = (SearchRequestCtx *)calloc(1, sizeof(SearchRequestCtx));
        ctx->coll = coll;
        ctx->time = coll->clock.time;
        ctx->index = idx;
        ctx->cpu_size_query = cpu_size / 10.0;
        ctx->mem_size_query = mem_size / 10.0;
        ctx->cpu_size_response = cpu_size;
        ctx->mem_size_response = mem_size;
        ctx->cpu_size_collate = cpu_size * 2.0;
        search_request_start(ctx);
    }

    // Schedule next arrival
    double rate = poisson_get_rate(gen, coll->clock.time);
    double inter_arrival = rng_expovariate(gen->rng, rate);
    clock_schedule_delay(&coll->clock, inter_arrival,
                         CONT(poisson_dispatch_next, gen));
}

void poisson_workload_start(PoissonWorkloadGenerator *gen) {
    // First arrival with inter-arrival delay
    double rate = poisson_get_rate(gen, gen->coll->clock.time);
    double inter_arrival = rng_expovariate(gen->rng, rate);
    clock_schedule_delay(&gen->coll->clock, inter_arrival,
                         CONT(poisson_dispatch_next, gen));
}

void poisson_workload_free(PoissonWorkloadGenerator *gen) {
    if (gen->save_file) {
        fclose(gen->save_file);
        gen->save_file = NULL;
    }
}
