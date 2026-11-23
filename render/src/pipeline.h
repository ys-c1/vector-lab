#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* 只做坐标变换，不落像素。并发路径与串行路径共用这一段，保证几何完全一致。 */
int pipeline_stage(const RScene *s, RPlace place, RScene *out);
#endif
