#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include "raster.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* 掩码布点路径：mask 为 16 位 4x4 子采样位掩码，全零时返回 -4，输出灰度覆盖率。 */
int pipeline_render_mask(const RScene *s, RPlace place, RBox win, int w, int h,
                         unsigned short mask, unsigned char *buf);
#endif
