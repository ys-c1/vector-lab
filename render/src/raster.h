#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
#include <stdint.h>

/* Equal-partition grid supersampling, binary 0/255 output. Frozen: its
 * output must never change. */
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);

/* Deterministic jittered supersampling with grayscale coverage values
 * (255 * hits / ss^2). Jitter sequence: SplitMix64 from `seed`, consumed
 * in fixed order t = y*ss + x within each pixel, ux then uy per sample.
 * Rejects seed == 0, n == 0 (here: ss < 1), and bad geometry/buffers. */
int raster_fill_jitter(const RScene *s, const RBox *box, int w, int h, int ss,
                       uint64_t seed, unsigned char *buf);
#endif
