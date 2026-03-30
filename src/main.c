#include "dse_sim.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <getopt.h>

static void print_usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s [options]\n"
        "\n"
        "Options:\n"
        "  --name NAME              Collection name (default: \"dse\")\n"
        "  --num-workers N          Number of workers (default: 2)\n"
        "  --half-ocus N            Half-OCUs per worker (default: 2)\n"
        "  --queue-length N         Queue capacity per worker (default: 100)\n"
        "  --cpu-frequency F        CPU frequency in Hz (default: 1e9)\n"
        "  --indices NAME[,NAME]    Comma-separated index names (default: \"default\")\n"
        "  --shards N[,N]           Shards per index (default: 6)\n"
        "  --workload PATH          JSONL workload file\n"
        "  --rate F                 Poisson arrival rate (if no workload file)\n"
        "  --rate-mean F            Sinusoidal mean rate\n"
        "  --rate-period F          Sinusoidal rate period\n"
        "  --end-time F             Simulation end time (for Poisson generator)\n"
        "  --cpu-mean F             Mean CPU cycles per request (default: 6.2e7)\n"
        "  --cpu-std F              Std of CPU cycles (default: 4e7)\n"
        "  --mem-mean F             Mean memory per request (default: 8e9)\n"
        "  --mem-std F              Std of memory (default: 1e8)\n"
        "  --profiler-output PATH   Profiler CSV output file\n"
        "  --profiler-period F      Profiling period in seconds (default: 300)\n"
        "  --profiler-first F       First profiler delay (default: 0)\n"
        "  --debug                  Enable debug output\n"
        "  -h, --help               Show this help\n",
        prog);
}

int main(int argc, char **argv) {
    // Defaults
    const char *name = "dse";
    int num_workers = 2;
    int half_ocus = 2;
    int queue_length = 100;
    double cpu_freq = 1e9;
    const char *indices_str = "default";
    const char *shards_str = "6";
    const char *workload_path = NULL;
    double rate = 0.0;
    double rate_mean = 0.0;
    double rate_period = 0.0;
    double end_time = 0.0;
    double cpu_mean = 6.2e7;
    double cpu_std = 4e7;
    double mem_mean = 8e9;
    double mem_std = 1e8;
    const char *profiler_output = NULL;
    double profiler_period = 300.0;
    double profiler_first = 0.0;
    bool debug = false;

    static struct option long_options[] = {
        {"name",             required_argument, 0, 'n'},
        {"num-workers",      required_argument, 0, 'w'},
        {"half-ocus",        required_argument, 0, 'o'},
        {"queue-length",     required_argument, 0, 'q'},
        {"cpu-frequency",    required_argument, 0, 'f'},
        {"indices",          required_argument, 0, 'i'},
        {"shards",           required_argument, 0, 's'},
        {"workload",         required_argument, 0, 'W'},
        {"rate",             required_argument, 0, 'r'},
        {"rate-mean",        required_argument, 0, 'M'},
        {"rate-period",      required_argument, 0, 'P'},
        {"end-time",         required_argument, 0, 'e'},
        {"cpu-mean",         required_argument, 0, 'C'},
        {"cpu-std",          required_argument, 0, 'c'},
        {"mem-mean",         required_argument, 0, 'R'},
        {"mem-std",          required_argument, 0, 'j'},
        {"profiler-output",  required_argument, 0, 'O'},
        {"profiler-period",  required_argument, 0, 'T'},
        {"profiler-first",   required_argument, 0, 'F'},
        {"debug",            no_argument,       0, 'd'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "h", long_options, NULL)) != -1) {
        switch (opt) {
            case 'n': name = optarg; break;
            case 'w': num_workers = atoi(optarg); break;
            case 'o': half_ocus = atoi(optarg); break;
            case 'q': queue_length = atoi(optarg); break;
            case 'f': cpu_freq = atof(optarg); break;
            case 'i': indices_str = optarg; break;
            case 's': shards_str = optarg; break;
            case 'W': workload_path = optarg; break;
            case 'r': rate = atof(optarg); break;
            case 'M': rate_mean = atof(optarg); break;
            case 'P': rate_period = atof(optarg); break;
            case 'e': end_time = atof(optarg); break;
            case 'C': cpu_mean = atof(optarg); break;
            case 'c': cpu_std = atof(optarg); break;
            case 'R': mem_mean = atof(optarg); break;
            case 'j': mem_std = atof(optarg); break;
            case 'O': profiler_output = optarg; break;
            case 'T': profiler_period = atof(optarg); break;
            case 'F': profiler_first = atof(optarg); break;
            case 'd': debug = true; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }

    // ─── Initialize ─────────────────────────────────────────────────────────

    Config config = config_default();
    config.debug = debug;
    config.cpu_frequency = cpu_freq;

    DSECollectionConfig dse_config = {
        .num_workers = num_workers,
        .half_ocus_per_worker = half_ocus,
        .cpu_frequency = cpu_freq,
        .queue_length = queue_length,
    };

    DSECollection coll;
    collection_init(&coll, name, dse_config, config);

    // ─── Create indices ─────────────────────────────────────────────────────

    // Parse comma-separated index names and shard counts
    char indices_buf[256], shards_buf[256];
    strncpy(indices_buf, indices_str, sizeof(indices_buf) - 1);
    strncpy(shards_buf, shards_str, sizeof(shards_buf) - 1);

    char *idx_names[DSE_MAX_INDICES];
    int idx_shards[DSE_MAX_INDICES];
    int num_idx = 0;

    char *tok = strtok(indices_buf, ",");
    while (tok && num_idx < DSE_MAX_INDICES) {
        idx_names[num_idx++] = tok;
        tok = strtok(NULL, ",");
    }

    int num_shard_specs = 0;
    tok = strtok(shards_buf, ",");
    while (tok && num_shard_specs < DSE_MAX_INDICES) {
        idx_shards[num_shard_specs++] = atoi(tok);
        tok = strtok(NULL, ",");
    }

    for (int i = 0; i < num_idx; i++) {
        Index *idx = (Index *)malloc(sizeof(Index));
        index_init(idx, &coll, idx_names[i]);
        collection_add_index_entry(&coll, idx_names[i], idx);

        int ns = (i < num_shard_specs) ? idx_shards[i] : 6;
        // Create ns shards per worker (matching Python's pattern)
        DynArray *workers = collection_workers(&coll);
        for (size_t w = 0; w < workers->size; w++) {
            Worker *worker = (Worker *)workers->items[w];
            index_create_shards(idx, worker, ns);
        }
    }

    // ─── Profiler ───────────────────────────────────────────────────────────

    Profiler profiler;
    profiler_init(&profiler, &coll, profiler_output, NULL, 0);
    profiler_run_periodically(&profiler, profiler_period, profiler_first);

    // ─── Workload ───────────────────────────────────────────────────────────

    LoadedWorkloadGenerator loaded_gen;
    PoissonWorkloadGenerator poisson_gen;
    bool using_loaded = false;

    if (workload_path) {
        int n = workload_load(&loaded_gen, &coll, workload_path);
        if (n < 0) {
            fprintf(stderr, "Failed to load workload\n");
            collection_free(&coll);
            return 1;
        }
        fprintf(stderr, "Loaded %d requests from %s\n", n, workload_path);
        workload_start(&loaded_gen);
        using_loaded = true;
    } else if (rate > 0.0 || rate_mean > 0.0) {
        if (end_time <= 0.0) {
            fprintf(stderr, "Error: --end-time required for Poisson generator\n");
            collection_free(&coll);
            return 1;
        }
        if (rate_mean > 0.0 && rate_period > 0.0) {
            poisson_workload_init_sinusoidal(&poisson_gen, &coll,
                rate_mean, rate_period, end_time,
                cpu_mean, cpu_std, mem_mean, mem_std, NULL);
        } else {
            poisson_workload_init(&poisson_gen, &coll, rate, end_time,
                cpu_mean, cpu_std, mem_mean, mem_std, NULL);
        }
        poisson_workload_start(&poisson_gen);
    } else {
        fprintf(stderr, "Error: specify --workload or --rate/--rate-mean\n");
        collection_free(&coll);
        return 1;
    }

    // ─── Run simulation ─────────────────────────────────────────────────────

    fprintf(stderr, "Starting simulation...\n");
    clock_run(&coll.clock);
    fprintf(stderr, "Simulation complete. Final time: %.4f\n", coll.clock.time);

    // ─── Cleanup ────────────────────────────────────────────────────────────

    profiler_free(&profiler);
    if (using_loaded) {
        workload_free(&loaded_gen);
    } else {
        poisson_workload_free(&poisson_gen);
    }
    collection_free(&coll);
    actor_id_counters_reset();

    return 0;
}
