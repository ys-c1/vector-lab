#ifndef XFORM_H
#define XFORM_H
#include <stddef.h>
typedef struct { double a, b, c, d, e, f; } XForm;
typedef struct { double x, y; } XPoint;
XForm xform_identity(void);
XPoint xform_apply(XForm m, XPoint p);
XForm xform_mul(XForm m, XForm n);

/* 一条缓存行容纳的 XPoint 个数：块宽按它对齐。 */
#define XFORM_CACHE_LINE 64u
#define XFORM_POINTS_PER_LINE (XFORM_CACHE_LINE / sizeof(XPoint))

/* 原地分块批量变换：pts 上就地读改写回，不申请第二份数组。
 * chunk 以点为单位，内部向上取整到缓存行倍数；chunk 传 0 拒绝。
 * 成功返回 NULL；拒绝时返回出错参数名："pts" / "n" / "chunk"。 */
const char *xform_batch_inplace(XForm m, XPoint *pts, size_t n, size_t chunk);
#endif
