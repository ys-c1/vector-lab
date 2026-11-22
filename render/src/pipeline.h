#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_place(const RScene *s, RPlace place, RScene *out);
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
#endif
