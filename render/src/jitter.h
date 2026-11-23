#ifndef RJITTER_H
#define RJITTER_H
#include <stdint.h>

/*
 * Deterministic jitter sequence, bit-identical on every platform.
 *
 * Spec (the same spec is reimplemented independently in gold/ref.c):
 *   - PRNG: SplitMix64. State starts at (uint64_t)seed. Each draw does
 *     state += 0x9E3779B97F4A7C15, then two multiply/xor/shift mixes and
 *     a final xor/shift; see jitter.c.
 *   - A 64-bit draw z maps to a double via (z >> 11) * 2^-53, range [0,1).
 *     Integer ops are exact and the multiply is by a power of two, so the
 *     resulting double is reproducible bit-for-bit.
 *   - Sample t consumes two draws, ux then uy.
 *
 * A zero seed or a non-positive length is rejected: randomness with no
 * seed cannot be reproduced, and an empty sequence has nothing to sample.
 */
typedef struct { double ux, uy; } RJitter;

/* Fills out[0..n) with deterministic pairs. Returns 0, or -1 on
 * seed == 0, n <= 0, or out == NULL. */
int jitter_gen(uint64_t seed, int n, RJitter *out);

/* Writes a text file: header "jitter1 <seed> <n>", then one
 * "%.17g %.17g" pair per line. Returns 0, -1 on bad arguments,
 * -2 if the file cannot be opened. */
int jitter_export(const char *path, uint64_t seed, int n);

#endif
