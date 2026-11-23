#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 只填充 [y0, y1) 这些行，其余行不动。像素取值与整图填充完全一致。 */
int raster_fill_rows(const RScene *s, const RBox *box, int w, int h, int ss,
                     int y0, int y1, unsigned char *buf);
#endif
