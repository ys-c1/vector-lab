#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);

/* 超采样覆盖率（0..255，每条扫描线 255*hit/(ss*ss)），供两档间过渡使用。 */
int raster_cover(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);

/* 两档覆盖率按权重 w 插值（w 为取 hi 的比例），结果四舍五入到 0..255。 */
int raster_blend(const unsigned char *lo, const unsigned char *hi, double w, int n, unsigned char *out);
#endif
