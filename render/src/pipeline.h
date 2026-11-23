#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* 可切换后端版本。backend 取值见 backend.h；非法选择器返回 -2。 */
int pipeline_render_backend(int backend, const RScene *s, RPlace place,
                            RBox win, int w, int h, unsigned char *buf);
#endif
