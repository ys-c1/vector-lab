#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
#define RRASTER_MAX_SS 8
/* 旧路径：任一点命中即填 255（二值），ss 只影响命中判定。 */
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 新路径：按覆盖率出灰阶，ss*ss 个子采样中命中的比例映射到 0..255。 */
int raster_coverage(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 点是否落在场景任一多边形内（并集语义）。 */
int raster_inside_scene(const RScene *s, double x, double y);
#endif
