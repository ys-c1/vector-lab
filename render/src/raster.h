#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
#endif
