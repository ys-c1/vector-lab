#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
#include "tilecache.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 带图块缓存的填充；tc 为 NULL 时等价于 raster_fill。
 * 输出与 raster_fill 逐字节一致：混合块用与 raster_fill 完全相同的全局坐标
 * 算式逐像素计算；不跨任何多边形边的块内外状态一致，角点在多边形内则
 * 整块恒为 255（FULL），否则恒为 0（EMPTY）。边 bbox 判定纯比较，无算术误差。 */
int raster_fill_tc(const RScene *s, const RBox *box, int w, int h, int ss,
                   unsigned char *buf, TileCache *tc);
#endif
