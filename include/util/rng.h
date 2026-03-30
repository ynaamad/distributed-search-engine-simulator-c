#ifndef DSE_RNG_H
#define DSE_RNG_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint64_t s[4];
    // Cached normal for Box-Muller
    double   spare;
    int      has_spare;
} RNG;

void   rng_seed(RNG *rng, uint64_t seed);
double rng_double(RNG *rng);              // uniform [0, 1)
int    rng_int(RNG *rng, int lo, int hi); // uniform [lo, hi] inclusive
double rng_uniform(RNG *rng, double lo, double hi);
double rng_gauss(RNG *rng, double mu, double sigma);
double rng_expovariate(RNG *rng, double lambda);
int    rng_choice_index(RNG *rng, int n);  // random index in [0, n)

#endif // DSE_RNG_H
