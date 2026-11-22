#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
/* 把场景按 place 变换到 out；返回 0，-2 缩放为零，-4 场景超容量。 */
int pipeline_transform(const RScene *s, RPlace place, RScene *out);
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
#endif
