#ifndef DSE_WORKLOAD_H
#define DSE_WORKLOAD_H

#include "types.h"
#include "event/continuation.h"
#include "util/rng.h"
#include <stdio.h>

// Parsed request descriptor from JSONL
typedef struct {
    RequestType type;
    double      time;
    char        index_name[64];
    int         document_id;
    double      cpu_size;
    double      mem_size;
    int         given_hash;
    bool        has_given_hash;
    // Search-specific
    double      cpu_size_query;
    double      mem_size_query;
    double      cpu_size_response;
    double      mem_size_response;
    double      cpu_size_collate;
    int        *given_hashes;
    int         num_given_hashes;
} RequestDescriptor;

typedef struct {
    DSECollection     *coll;
    RequestDescriptor *requests;
    int                num_requests;
    int                current;
    bool               active;
} LoadedWorkloadGenerator;

// Load workload from JSONL file
int workload_load(LoadedWorkloadGenerator *gen, DSECollection *coll,
                  const char *path);

// Start dispatching requests (self-scheduling)
void workload_start(LoadedWorkloadGenerator *gen);

// Free workload data
void workload_free(LoadedWorkloadGenerator *gen);

// ─── Poisson-based generators ───────────────────────────────────────────────

typedef struct {
    DSECollection *coll;
    RNG           *rng;        // points to collection's RNG
    double         rate;       // for constant rate
    // For sinusoidal:
    double         rate_mean;
    double         rate_period;
    bool           sinusoidal;
    // CPU/mem size generation
    double         cpu_mean;
    double         cpu_std;
    double         mem_mean;
    double         mem_std;
    // State
    int            count;
    double         end_time;
    bool           active;
    // Save
    FILE          *save_file;
} PoissonWorkloadGenerator;

void poisson_workload_init(PoissonWorkloadGenerator *gen, DSECollection *coll,
                           double rate, double end_time,
                           double cpu_mean, double cpu_std,
                           double mem_mean, double mem_std,
                           const char *save_path);

void poisson_workload_init_sinusoidal(PoissonWorkloadGenerator *gen,
                                      DSECollection *coll,
                                      double rate_mean, double rate_period,
                                      double end_time,
                                      double cpu_mean, double cpu_std,
                                      double mem_mean, double mem_std,
                                      const char *save_path);

void poisson_workload_start(PoissonWorkloadGenerator *gen);
void poisson_workload_free(PoissonWorkloadGenerator *gen);

#endif // DSE_WORKLOAD_H
