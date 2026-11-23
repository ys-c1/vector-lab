#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include <stdint.h>
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* Deterministic jittered render; seed == 0 is rejected (-5). */
int pipeline_render_jitter(const RScene *s, RPlace place, RBox win, int w, int h,
                           int ss, uint64_t seed, unsigned char *buf);
#endif
