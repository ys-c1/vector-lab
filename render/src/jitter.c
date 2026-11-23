#include "jitter.h"
#include <stdio.h>

static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static double draw_u01(uint64_t *state) {
    return (double)(splitmix64(state) >> 11) * 0x1.0p-53;
}

int jitter_gen(uint64_t seed, int n, RJitter *out) {
    if (seed == 0 || n <= 0 || !out) return -1;
    uint64_t state = seed;
    for (int t = 0; t < n; ++t) {
        out[t].ux = draw_u01(&state);
        out[t].uy = draw_u01(&state);
    }
    return 0;
}

int jitter_export(const char *path, uint64_t seed, int n) {
    if (!path || seed == 0 || n <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "jitter1 %llu %d\n", (unsigned long long)seed, n);
    uint64_t state = seed;
    for (int t = 0; t < n; ++t) {
        double ux = draw_u01(&state);
        double uy = draw_u01(&state);
        fprintf(f, "%.17g %.17g\n", ux, uy);
    }
    fclose(f);
    return 0;
}
