#ifndef RFIXED_H
#define RFIXED_H
#include "scene.h"
#include "pipeline.h"

/* 坐标 |v|*2^q 的安全上限（2 的幂次指数）：int64 留 2 位余量，
 * 保证差值、求和与 __int128 中间积不溢出。 */
#define FX_INT_LIM_BIT 61

/* 由目标绝对精度 eps（场景单位）反解二进制小数位数 q：量化步 2^-q <= eps，
 * 单次量化舍入误差 <= 2^-(q+1)。eps 非法返回 -1。q 上限 40。 */
int fixed_bits_for_eps(double eps);

/* 溢出体检：场景顶点与窗口边界在 q 位小数下是否超出安全范围。
 * 返回 0 正常，1 溢出；*worst 为最劣坐标量级，*qmax 为该量级允许的最大 q。 */
int fixed_check_overflow(const RScene *s, const RBox *win, int q, double *worst, int *qmax);

/* 定点光栅化：坐标量化到 q 位小数后全程整数比较，跨机器逐字节确定。
 * 返回 -1 参数错，-4 坐标溢出，-5 q 越界。 */
int raster_fill_fixed(const RScene *s, const RBox *box, int w, int h, int ss, int q, unsigned char *buf);

/* 定点管线：变换系数与坐标全部量化，整数变换 + 定点光栅，ss 固定 4 与浮点管线一致。
 * 返回码同 pipeline_render，另 -4 溢出，-5 q 越界。 */
int pipeline_render_fixed(const RScene *s, RPlace place, RBox win, int w, int h, int q, unsigned char *buf);

#endif
