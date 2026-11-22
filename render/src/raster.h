#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
#include "cull.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 带剔除的填充：size==0 的多边形免费剔除，size < threshold 的按阈值剔除。
 * threshold <= 0 且无需免费剔除时，输出与 raster_fill 逐字节一致。 */
int raster_fill_cull(const RScene *s, const RBox *box, int w, int h, int ss,
                     double threshold, RCullStats *st, unsigned char *buf);
/* 单个多边形实际点亮的像素数，采样网格与 raster_fill 完全相同。 */
int raster_poly_coverage(const RPoly *p, const RBox *box, int w, int h, int ss);
#endif
