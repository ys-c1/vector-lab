#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 覆盖率采样（新逻辑）：buf = 255 * 命中子采样数 / (ss*ss)，输出灰度。
 * phx/phy 为像素内采样相位，0.5 表示居中；旧 raster_fill 保持不变。 */
int raster_fill_cov(const RScene *s, const RBox *box, int w, int h, int ss,
                    double phx, double phy, unsigned char *buf);
#endif
