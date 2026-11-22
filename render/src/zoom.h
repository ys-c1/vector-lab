#ifndef RZOOM_H
#define RZOOM_H
#include "scene.h"

/* 连续对数缩放：层号 level = log2(scale)，允许小数。
 * 小数层号拆成 lo、hi 两档，w = level - lo 为取 hi 档的比例。 */
typedef struct {
    double level;   /* log2(scale) */
    int lo;         /* 下档整数层 */
    int hi;         /* 上档整数层 = lo + 1 */
    double w;       /* 过渡权重：取 hi 档的比例，0..1 */
    int exact;      /* 层号恰为整数（容差内），此时 w 为 0 */
} RZoomLevel;

/* 把小数层号拆成两档与权重。返回 0 成功，-1 参数错误或层号超出范围。 */
int zoom_level_split(double level, RZoomLevel *out);

/* 由缩放反解层号。scale 必须为有限正数：
 * 返回 0 成功，-1 参数错误，-2 scale 为零或负，-3 scale 非有限。 */
int zoom_level_from_scale(double scale, RZoomLevel *out);

/* 整数层对应的缩放 2^level；整数层下按位构造，结果精确。 */
double zoom_scale_of_level(int level);

/* 任意层号对应的缩放 2^level（连续对数，无跳变）。 */
double zoom_scale_of_level_frac(double level);

#endif
