#include "zoom.h"
#include <math.h>

/* 层号与整数的距离小于此值时视为恰好落在整数层。
 * 1e-9 远小于相邻层间距 1，又足以吸收 log2(2^k) 的舍入噪声。 */
#define LEVEL_EPS 1e-9

int zoom_level_split(double level, RZoomLevel *out) {
    if (!out || !isfinite(level)) return -1;
    double fl = floor(level);
    if (fl < -1074.0 || fl > 1023.0) return -1; /* 2^level 超出可表示范围 */
    double w = level - fl;
    int lo = (int)fl;
    if (w < LEVEL_EPS) {
        out->level = level;
        out->lo = lo;
        out->hi = lo + 1;
        out->w = 0.0;
        out->exact = 1;
        return 0;
    }
    if (w > 1.0 - LEVEL_EPS) {
        /* 舍入把层号顶到上一档，归为上一档的单档。 */
        out->level = level;
        out->lo = lo + 1;
        out->hi = lo + 2;
        out->w = 0.0;
        out->exact = 1;
        return 0;
    }
    out->level = level;
    out->lo = lo;
    out->hi = lo + 1;
    out->w = w;
    out->exact = 0;
    return 0;
}

int zoom_level_from_scale(double scale, RZoomLevel *out) {
    if (!out) return -1;
    if (!isfinite(scale)) return -3;
    if (!(scale > 0.0)) return -2; /* 0、负数和 -0 都在此被拒 */
    return zoom_level_split(log2(scale), out);
}

double zoom_scale_of_level(int level) {
    /* 整数层按位拼出 2^level，避免 pow 在这里引入误差。 */
    if (level >= -1022 && level <= 1023) {
        union { double d; unsigned long long u; } v;
        v.u = (unsigned long long)(level + 1023) << 52;
        return v.d;
    }
    return ldexp(1.0, level);
}

double zoom_scale_of_level_frac(double level) {
    return exp2(level);
}
