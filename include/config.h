#ifndef DSE_CONFIG_H
#define DSE_CONFIG_H

#include <stdbool.h>

typedef struct {
    double cpu_spin_up_delay;
    double cpu_spin_down_delay;
    double worker_ready_time;
    double worker_spin_down_time;
    double compute_hash_timeout;
    double disk_read_speed;
    double disk_write_speed;
    double index_creation_time;
    double shard_creation_time;
    double memory_per_half_ocu;
    double cpu_frequency;
    double blue_green_cpu_util;
    bool   ignore_steady;
    bool   debug;
} Config;

static inline Config config_default(void) {
    return (Config){
        .cpu_spin_up_delay    = 220.0,
        .cpu_spin_down_delay  = 0.0,
        .worker_ready_time    = 0.0,
        .worker_spin_down_time = 0.0,
        .compute_hash_timeout = 1e-5,
        .disk_read_speed      = 1e9,
        .disk_write_speed     = 1e9,
        .index_creation_time  = 60.0,
        .shard_creation_time  = 0.0,
        .memory_per_half_ocu  = 2.0e9,
        .cpu_frequency        = 1.0e9,
        .blue_green_cpu_util  = 0.7,
        .ignore_steady        = false,
        .debug                = false,
    };
}

typedef struct {
    int    num_workers;
    int    half_ocus_per_worker;
    double cpu_frequency;
    int    queue_length;
} DSECollectionConfig;

static inline int dse_config_cpus_per_worker(const DSECollectionConfig *c) {
    int half = c->half_ocus_per_worker / 2;
    return half > 1 ? half : 1;
}

static inline double dse_config_memory_per_worker(const DSECollectionConfig *c, const Config *cfg) {
    return c->half_ocus_per_worker * cfg->memory_per_half_ocu;
}

#endif // DSE_CONFIG_H
