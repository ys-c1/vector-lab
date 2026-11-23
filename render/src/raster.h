#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"

/* 基线光栅化：原有表达式与顺序。 */
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);

/* 确定性光栅化：fixed=1 时禁用乘加合并、固定求值顺序；fixed=0 等同基线。 */
int raster_fill_cfg(const RScene *s, const RBox *box, int w, int h, int ss,
                    int fixed, unsigned char *buf);
#endif
