#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

/* 显式开启凸包精简的渲染入口：每个多边形先按 area_ratio 做面积保持型
 * 顶点精简（见 hull/simplify），再走与 pipeline_render 相同的变换与光栅化。
 * area_ratio 必须在 (0,1]；退化多边形（严格凸包 < 3 顶点）返回 -5。
 * 不传 ratio 时永远使用 pipeline_render，老路径逐字节不变。 */
int pipeline_render_simplified(const RScene *s, RPlace place, RBox win,
                                int w, int h, double area_ratio,
                                unsigned char *buf, int *removed_out);
#endif
