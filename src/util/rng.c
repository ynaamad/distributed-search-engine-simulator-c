#include "util/rng.h"
#include <math.h>

// SplitMix64 for seeding
static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

void rng_seed(RNG *rng, uint64_t seed) {
    uint64_t sm = seed;
    rng->s[0] = splitmix64(&sm);
    rng->s[1] = splitmix64(&sm);
    rng->s[2] = splitmix64(&sm);
    rng->s[3] = splitmix64(&sm);
    rng->has_spare = 0;
    rng->spare = 0.0;
}

static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

// xoshiro256**
static uint64_t rng_next(RNG *rng) {
    uint64_t *s = rng->s;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;
    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);
    return result;
}

double rng_double(RNG *rng) {
    return (double)(rng_next(rng) >> 11) * 0x1.0p-53;
}

int rng_int(RNG *rng, int lo, int hi) {
    if (lo >= hi) return lo;
    uint64_t range = (uint64_t)(hi - lo) + 1;
    return lo + (int)(rng_next(rng) % range);
}

double rng_uniform(RNG *rng, double lo, double hi) {
    return lo + rng_double(rng) * (hi - lo);
}

double rng_gauss(RNG *rng, double mu, double sigma) {
    // Box-Muller transform with caching
    if (rng->has_spare) {
        rng->has_spare = 0;
        return mu + sigma * rng->spare;
    }
    double u, v, s;
    do {
        u = rng_double(rng) * 2.0 - 1.0;
        v = rng_double(rng) * 2.0 - 1.0;
        s = u * u + v * v;
    } while (s >= 1.0 || s == 0.0);
    double mul = sqrt(-2.0 * log(s) / s);
    rng->spare = v * mul;
    rng->has_spare = 1;
    return mu + sigma * u * mul;
}

double rng_expovariate(RNG *rng, double lambda) {
    double u;
    do {
        u = rng_double(rng);
    } while (u == 0.0);
    return -log(u) / lambda;
}

int rng_choice_index(RNG *rng, int n) {
    return (int)(rng_next(rng) % (uint64_t)n);
}
