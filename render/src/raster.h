#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"

/* 4x4 子采样位掩码：bit (4*row + col)，row/col 均为 0..3，
 * 置位的格子在 (col+0.5)/4, (row+0.5)/4 处布一个子采样点。 */
#define RMASK_BITS 16

/* 解析恰好 16 个 '0'/'1' 字符（行优先 4x4）。
 * 返回 0 成功；-1 空指针；-2 位数不对；-3 含非 0/1 字符。 */
int raster_mask_parse(const char *s, unsigned short *out);

/* 掩码中置位的子点个数（0..16）。 */
int raster_mask_count(unsigned short mask);

/* 按掩码布点的覆盖率光栅化：输出灰度 255*hit/K（K=置位数）。
 * 全零掩码无采样点，返回 -4；其余参数错误同 raster_fill。 */
int raster_fill_mask(const RScene *s, const RBox *box, int w, int h,
                     unsigned short mask, unsigned char *buf);

int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
#endif
