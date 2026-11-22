#include "isect.h"
#include <math.h>

static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out) {
    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double den = cross2(rx, ry, sx, sy);
    if (fabs(den) < 1e-12) return 0;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    if (out) {
        out->t = t; out->u = u;
        out->at.x = a0.x + rx * t;
        out->at.y = a0.y + ry * t;
    }
    return 1;
}

int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits) {
    if (!a || !b || !hits || na < 2 || nb < 2) return -1;
    int k = 0;
    for (int i = 0; i + 1 < na; ++i)
        for (int j = 0; j + 1 < nb; ++j)
            if (isect_seg(a[i], a[i+1], b[j], b[j+1], 0)) ++k;
    *hits = k;
    return 0;
}

int isect_self(const IPoint *p, int n, int *first_a, int *first_b) {
    if (!p || !first_a || !first_b || n < 4) return -1;
    for (int i = 0; i + 1 < n; ++i) {
        for (int j = i + 2; j + 1 < n; ++j) {
            if (i == 0 && j == n - 2) continue;
            if (isect_seg(p[i], p[i+1], p[j], p[j+1], 0)) {
                *first_a = i; *first_b = j;
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- 新路径：最小距离谓词，不使用任何固定阈值 ---------- */

int isect_zero_len(IPoint a0, IPoint a1) {
    return a0.x == a1.x && a0.y == a1.y;
}

static double dist2_pt_seg(double px, double py, IPoint a, IPoint b) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double len2 = vx * vx + vy * vy;
    if (len2 == 0.0) {
        double dx = px - a.x, dy = py - a.y;
        return dx * dx + dy * dy;
    }
    double t = ((px - a.x) * vx + (py - a.y) * vy) / len2;
    if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
    double cx = a.x + vx * t, cy = a.y + vy * t;
    double dx = px - cx, dy = py - cy;
    return dx * dx + dy * dy;
}

static int collinear_overlap(IPoint a0, IPoint a1, IPoint b0, IPoint b1) {
    /* 调用方已保证四点共线；x 不退化就按 x 判区间，否则按 y */
    if (a0.x != a1.x || b0.x != b1.x) {
        double alo = fmin(a0.x, a1.x), ahi = fmax(a0.x, a1.x);
        double blo = fmin(b0.x, b1.x), bhi = fmax(b0.x, b1.x);
        return ahi >= blo && bhi >= alo;
    }
    double alo = fmin(a0.y, a1.y), ahi = fmax(a0.y, a1.y);
    double blo = fmin(b0.y, b1.y), bhi = fmax(b0.y, b1.y);
    return ahi >= blo && bhi >= alo;
}

double isect_gap(IPoint a0, IPoint a1, IPoint b0, IPoint b1) {
    int za = isect_zero_len(a0, a1), zb = isect_zero_len(b0, b1);
    if (za && zb) {
        double dx = a0.x - b0.x, dy = a0.y - b0.y;
        return hypot(dx, dy);
    }
    if (za) return sqrt(dist2_pt_seg(a0.x, a0.y, b0, b1));
    if (zb) return sqrt(dist2_pt_seg(b0.x, b0.y, a0, a1));

    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    double den = cross2(rx, ry, sx, sy);
    if (den == 0.0) {
        /* 精确平行：共线重叠为 0，否则最小距离在端点处取得 */
        if (cross2(qx, qy, rx, ry) == 0.0 && collinear_overlap(a0, a1, b0, b1))
            return 0.0;
        double d2 = dist2_pt_seg(a0.x, a0.y, b0, b1);
        double v;
        if ((v = dist2_pt_seg(a1.x, a1.y, b0, b1)) < d2) d2 = v;
        if ((v = dist2_pt_seg(b0.x, b0.y, a0, a1)) < d2) d2 = v;
        if ((v = dist2_pt_seg(b1.x, b1.y, a0, a1)) < d2) d2 = v;
        return sqrt(d2);
    }
    /* 非平行（含近平行：den 极小非零）。求无限直线交点参数 t（沿 r）、
     * u（沿 s）。两参数都在 [0,1] 内即线段相交，gap 为 0；否则把越界参数
     * 钳到区间，钳位点为最近点对候选。近平行下钳制距离仍稳健，且再与四端点
     * 垂距取 min 兜底，保证不遗漏边界最近点。全程无固定阈值。 */
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    if (t >= 0.0 && t <= 1.0 && u >= 0.0 && u <= 1.0) return 0.0;
    double tc = t < 0.0 ? 0.0 : t > 1.0 ? 1.0 : t;
    double uc = u < 0.0 ? 0.0 : u > 1.0 ? 1.0 : u;
    double cx = a0.x + rx * tc, cy = a0.y + ry * tc;
    double dx = (b0.x + sx * uc) - cx, dy = (b0.y + sy * uc) - cy;
    double d2 = dx * dx + dy * dy;
    double v;
    if ((v = dist2_pt_seg(a0.x, a0.y, b0, b1)) < d2) d2 = v;
    if ((v = dist2_pt_seg(a1.x, a1.y, b0, b1)) < d2) d2 = v;
    if ((v = dist2_pt_seg(b0.x, b0.y, a0, a1)) < d2) d2 = v;
    if ((v = dist2_pt_seg(b1.x, b1.y, a0, a1)) < d2) d2 = v;
    return sqrt(d2);
}
