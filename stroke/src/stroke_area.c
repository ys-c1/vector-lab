/* 按目标覆盖面积反解线宽（新逻辑，编译开关 STROKE_AREA_FIT）。
 *
 * 关闭开关时本文件是空编译单元，不产生任何符号；stroke/src/stroke.c
 * 的固定宽度路径未被触碰，`make demo` / `make regress` 的产物逐字节不变。
 */
#ifdef STROKE_AREA_FIT

#define _POSIX_C_SOURCE 199309L
#include "stroke_area.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SA_PI 3.14159265358979323846
#define SA_EPS 1e-12
#define SA_MAX_ITER     120
#define SA_MODEL_TOL    0.005  /* 模型解与实测相差超过 0.5% 即改走实测 */
#define SA_FOLD_LIMIT   (SA_PI - 1e-3) /* 转角接近 π：路径折返，模型失效 */

/* ---------- 基础工具 ---------- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec / 1e6;
}

const char *stroke_area_strerror(int code) {
    switch (code) {
    case STROKE_AREA_OK:             return "ok";
    case STROKE_AREA_E_ARG:          return "invalid argument: null pointer, non-finite point, or n out of range";
    case STROKE_AREA_E_WIDTH:        return "invalid argument: width bounds must satisfy 0 <= w_min < w_max (negative width rejected)";
    case STROKE_AREA_E_ZERO_LEN:     return "invalid argument: path length is zero (all points coincide)";
    case STROKE_AREA_E_TARGET:       return "invalid argument: target area must be finite and > 0";
    case STROKE_AREA_E_NO_SOLUTION:  return "no solution: target area unreachable within [w_min, w_max]";
    case STROKE_AREA_E_CONVERGE:     return "convergence failure: measured inversion exceeded iteration limit";
    default:                         return "unknown error";
    }
}

static double dist(SPoint a, SPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

/* 去掉相邻重合点；闭合路径首尾重合也一并去掉。 */
static int compact(const SPoint *pts, int n, int closed, SPoint *dst) {
    int k = 0;
    for (int i = 0; i < n; ++i) {
        if (k > 0 && dist(pts[i], dst[k - 1]) <= 1e-12) continue;
        dst[k++] = pts[i];
    }
    if (closed && k > 1 && dist(dst[0], dst[k - 1]) <= 1e-12) --k;
    return k;
}

typedef struct {
    int n;          /* 抽稀后点数 */
    int closed;
    int m;          /* 段数：闭合 n 段，开放 n-1 段 */
    double length;  /* 中心线总长 L */
    double quad;    /* 二次项 k = (cap - C)/4 */
    int fold;       /* 1 = 存在接近 π 的折返，二次模型不适用 */
} SGeom;

static SPoint pt(const SPoint *p, int n, int i) { return p[i % n]; }

static int build_geom(const SPoint *p, int n, int closed, SGeom *g) {
    g->n = n;
    g->closed = closed;
    g->m = closed ? n : n - 1;
    if (g->m < 1) return STROKE_AREA_E_ARG;
    double len = 0.0;
    for (int i = 0; i < g->m; ++i) {
        SPoint a = pt(p, n, i), b = pt(p, n, i + 1);
        len += dist(a, b);
    }
    if (!(len > SA_EPS) || !isfinite(len)) return STROKE_AREA_E_ZERO_LEN;
    g->length = len;
    g->fold = 0;

    double C = 0.0;
    int first = closed ? 0 : 1;
    for (int i = first; i < g->m; ++i) {
        int ip = (i - 1 + g->m) % g->m;
        SPoint a0 = pt(p, n, ip), a1 = pt(p, n, ip + 1);
        SPoint b0 = pt(p, n, i),  b1 = pt(p, n, i + 1);
        double ux = a1.x - a0.x, uy = a1.y - a0.y;
        double vx = b1.x - b0.x, vy = b1.y - b0.y;
        double lu = sqrt(ux * ux + uy * uy), lv = sqrt(vx * vx + vy * vy);
        if (lu <= SA_EPS || lv <= SA_EPS) continue;
        double cr = (ux * vy - uy * vx) / (lu * lv);
        double dt = (ux * vx + uy * vy) / (lu * lv);
        if (cr < -1.0) cr = -1.0; else if (cr > 1.0) cr = 1.0;
        if (dt < -1.0) dt = -1.0; else if (dt > 1.0) dt = 1.0;
        double phi = fabs(atan2(cr, dt));
        if (phi > SA_FOLD_LIMIT) { g->fold = 1; continue; }
        if (phi > SA_EPS) C += tan(phi * 0.5) - phi * 0.5;
    }
    double cap = closed ? 0.0 : SA_PI;
    g->quad = 0.25 * (cap - C);
    return STROKE_AREA_OK;
}

static double model_area(const SGeom *g, double width) {
    return g->length * width + g->quad * width * width;
}

/* ---------- 实测：逐行胶囊并集 + Simpson 积分 ----------
 * 覆盖面积 = 中心线各段胶囊体（矩形 + 两端半圆）的并集面积。
 * 每条水平线与一个凸胶囊的交集是一个区间；整行就是这些区间的并。
 * 与二次模型完全独立：模型是解析积分，这里是逐行数值积分。 */

typedef struct { double lo, hi; } SInterval;

static int capsule_row(SPoint a, SPoint b, double h, double y, SInterval *iv) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double l = sqrt(dx * dx + dy * dy);
    if (l <= SA_EPS) return 0;
    double tx = dx / l, ty = dy / l;
    double nx = -ty, ny = tx;
    double lo = -HUGE_VAL, hi = HUGE_VAL;
    for (int c = 0; c < 2; ++c) {
        double A = c ? nx : tx, Ay = c ? ny : ty;
        double lo_c = c ? -h : 0.0, hi_c = c ? h : l;
        double off = c ? (nx * a.x + ny * a.y) : (tx * a.x + ty * a.y);
        double xlo = lo_c - Ay * y + off;
        double xhi = hi_c - Ay * y + off;
        if (fabs(A) <= 1e-14) {
            if (xlo > 0.0 || xhi < 0.0) return 0;
        } else {
            double x1 = xlo / A, x2 = xhi / A;
            if (x1 > x2) { double t = x1; x1 = x2; x2 = t; }
            if (x1 > lo) lo = x1;
            if (x2 < hi) hi = x2;
        }
    }
    if (!(hi > lo)) return 0;
    iv->lo = lo;
    iv->hi = hi;
    return 1;
}

/* 两端圆盘与水平线的交区间；矩形部分已覆盖 [0,l]，补圆盘才成为圆头胶囊。 */
static int cap_row(SPoint c, double h, double y, SInterval *iv) {
    double d2 = h * h - (y - c.y) * (y - c.y);
    if (d2 <= 0.0) return 0;
    double r = sqrt(d2);
    iv->lo = c.x - r;
    iv->hi = c.x + r;
    return 1;
}

static void sort_iv(SInterval *v, int k) { /* k 很小，插入排序足够 */
    for (int i = 1; i < k; ++i) {
        SInterval x = v[i];
        int j = i - 1;
        while (j >= 0 && v[j].lo > x.lo) { v[j + 1] = v[j]; --j; }
        v[j + 1] = x;
    }
}

static double row_union(const SPoint *p, const SGeom *g, double h, double y,
                        SInterval *iv) {
    int k = 0;
    for (int i = 0; i < g->m; ++i) {
        SPoint a = pt(p, g->n, i), b = pt(p, g->n, i + 1);
        if (capsule_row(a, b, h, y, &iv[k])) ++k;
        if (cap_row(a, h, y, &iv[k])) ++k;
        if (cap_row(b, h, y, &iv[k])) ++k;
    }
    if (k == 0) return 0.0;
    sort_iv(iv, k);
    double total = 0.0, cl = iv[0].lo, ch = iv[0].hi;
    for (int i = 1; i < k; ++i) {
        if (iv[i].lo > ch) {
            total += ch - cl;
            cl = iv[i].lo;
            ch = iv[i].hi;
        } else if (iv[i].hi > ch) {
            ch = iv[i].hi;
        }
    }
    return total + (ch - cl);
}

static int geom_for_measure(const SPoint *pts, int npts, int closed,
                            SPoint *p, SGeom *g) {
    int n = compact(pts, npts, closed, p);
    /* 抽稀后不足两点 = 所有点重合：零长度，先于一切几何判定 */
    if (n < 2) return STROKE_AREA_E_ZERO_LEN;
    return build_geom(p, n, closed, g);
}

/* 逐行并集的被积函数；iv 由调用方提供，容量 >= 3*m+3。 */
typedef struct {
    const SPoint *p;
    const SGeom *g;
    double h;
    SInterval *iv;
} SRowCtx;

static double row_f(const SRowCtx *c, double y) {
    return row_union(c->p, c->g, c->h, y, c->iv);
}

/* 自适应 Simpson：每个子区间按误差估计递归加密。圆弧端头在 y=c.y±h 处
 * 有平方根型拐点，调用方先按这些点把积分区间切开，再自适应细分。 */
static double adapt_simpson(const SRowCtx *c, double a, double b,
                            double fa, double fm, double fb, double whole,
                            double eps, int depth) {
    double m = 0.5 * (a + b);
    double lm = 0.5 * (a + m), rm = 0.5 * (m + b);
    double flm = row_f(c, lm), frm = row_f(c, rm);
    double left = (m - a) / 6.0 * (fa + 4.0 * flm + fm);
    double right = (b - m) / 6.0 * (fm + 4.0 * frm + fb);
    double both = left + right;
    if (depth <= 0 || fabs(both - whole) <= 15.0 * eps)
        return both + (both - whole) / 15.0;
    return adapt_simpson(c, a, m, fa, flm, fm, left, eps * 0.5, depth - 1)
         + adapt_simpson(c, m, b, fm, frm, fb, right, eps * 0.5, depth - 1);
}

static int cmp_double(const void *pa, const void *pb) {
    double a = *(const double *)pa, b = *(const double *)pb;
    return (a > b) - (a < b);
}

/* 把一段积分拆成以断点分割的若干子区间，逐段自适应积分。 */
static double integrate_piecewise(const SRowCtx *c, const double *bp, int nbp,
                                  double eps) {
    double total = 0.0;
    for (int k = 0; k + 1 < nbp; ++k) {
        double a = bp[k], b = bp[k + 1];
        if (!(b > a)) continue;
        double fa = row_f(c, a), fb = row_f(c, b);
        double m = 0.5 * (a + b), fm = row_f(c, m);
        double whole = (b - a) / 6.0 * (fa + 4.0 * fm + fb);
        double scale = fabs(whole) > 1.0 ? fabs(whole) : 1.0;
        total += adapt_simpson(c, a, b, fa, fm, fb, whole, eps * scale, 44);
    }
    return total;
}

double stroke_area_measure(const SPoint *pts, int npts, int closed,
                           double width, double rel_tol) {
    if (!pts || npts < 2 || npts > STROKE_AREA_MAX_PTS) return -1.0;
    if (!(width >= 0.0) || !isfinite(width)) return -1.0;
    if (width == 0.0) return 0.0;   /* 零宽度的覆盖面积定义为零 */
    for (int i = 0; i < npts; ++i)
        if (!isfinite(pts[i].x) || !isfinite(pts[i].y)) return -1.0;

    SPoint p[STROKE_AREA_MAX_PTS];
    SGeom g;
    if (geom_for_measure(pts, npts, closed, p, &g) != STROKE_AREA_OK) return -1.0;

    double h = width * 0.5;
    double ymin = p[0].y, ymax = p[0].y;
    for (int i = 1; i < g.n; ++i) {
        if (p[i].y < ymin) ymin = p[i].y;
        if (p[i].y > ymax) ymax = p[i].y;
    }
    ymin -= h;
    ymax += h;
    if (!(ymax > ymin) || !isfinite(ymin) || !isfinite(ymax)) return -1.0;

    /* 断点：每个端点的圆头切点 y=c.y±h，外加两端；排序去重。 */
    int nbp = 2;
    double *bp = (double *)malloc(sizeof(double) * (size_t)(4 * g.n + 2));
    SInterval *iv = (SInterval *)malloc(sizeof(SInterval) * (size_t)(3 * g.m + 3));
    if (!bp || !iv) { free(bp); free(iv); return -1.0; }
    bp[0] = ymin;
    bp[1] = ymax;
    for (int i = 0; i < g.n; ++i) {
        double lo = p[i].y - h, hi = p[i].y + h;
        if (lo > ymin && lo < ymax) bp[nbp++] = lo;
        if (hi > ymin && hi < ymax) bp[nbp++] = hi;
    }
    qsort(bp, (size_t)nbp, sizeof(double), cmp_double);
    int u = 1;
    for (int i = 1; i < nbp; ++i)
        if (bp[i] - bp[u - 1] > 1e-12 * (1.0 + fabs(bp[i]))) bp[u++] = bp[i];

    /* 自适应积分的相对精度。rel_tol <= 0 取默认 1e-9，比目标的 1% 高七个
     * 数量级；传入更小的正值可再收紧。 */
    double eps = (rel_tol > 0.0 && isfinite(rel_tol)) ? rel_tol : 1e-9;
    if (eps < 1e-12) eps = 1e-12;
    if (eps > 1e-3) eps = 1e-3;

    SRowCtx ctx = { p, &g, h, iv };
    double area = integrate_piecewise(&ctx, bp, u, eps);
    free(bp);
    free(iv);
    return area;
}


/* ---------- 自交检测 ---------- */

static double cross2(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static int segs_cross(SPoint p, SPoint q, SPoint r, SPoint s, SPoint *at) {
    double rx = q.x - p.x, ry = q.y - p.y;
    double sx = s.x - r.x, sy = s.y - r.y;
    double den = cross2(rx, ry, sx, sy);
    if (fabs(den) < 1e-12) return 0;              /* 平行或共线，不计 */
    double qx = r.x - p.x, qy = r.y - p.y;
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    const double e = 1e-9;
    if (t <= e || t >= 1.0 - e || u <= e || u >= 1.0 - e) return 0;
    if (at) { at->x = p.x + rx * t; at->y = p.y + ry * t; }
    return 1;
}

static int find_self(const SPoint *p, const SGeom *g, int *ia, int *ib, SPoint *at) {
    for (int i = 0; i + 1 < g->m; ++i) {
        for (int j = i + 1; j < g->m; ++j) {
            if (j == i + 1) continue;
            if (g->closed && i == 0 && j == g->m - 1) continue;
            SPoint a = pt(p, g->n, i), b = pt(p, g->n, i + 1);
            SPoint c = pt(p, g->n, j), d = pt(p, g->n, j + 1);
            if (segs_cross(a, b, c, d, at)) {
                if (ia) *ia = i;
                if (ib) *ib = j;
                return 1;
            }
        }
    }
    return 0;
}

/* ---------- 实测反解（二分） ---------- */

static int solve_measured_real(const SPoint *p, int n, int closed,
                               double target, double w_lo, double w_hi, double tol,
                               double *w_out, double *a_out, int *iters) {
    if (iters) *iters = 0;
    double alo = stroke_area_measure(p, n, closed, w_lo, tol);
    double ahi = stroke_area_measure(p, n, closed, w_hi, tol);
    if (alo < 0.0 || ahi < 0.0) return STROKE_AREA_E_ARG;
    if (target < alo - 1e-9 * (1.0 + target)) return STROKE_AREA_E_NO_SOLUTION;
    if (target > ahi + 1e-9 * (1.0 + target)) return STROKE_AREA_E_NO_SOLUTION;

    double lo = w_lo, hi = w_hi;
    if (target <= alo) {
        *w_out = w_lo;
        *a_out = alo;
        return STROKE_AREA_OK;
    }
    for (int it = 0; it < SA_MAX_ITER; ++it) {
        double mid = 0.5 * (lo + hi);
        double a = stroke_area_measure(p, n, closed, mid, tol);
        if (a < 0.0) return STROKE_AREA_E_ARG;
        if (iters) *iters = it + 1;
        double scale = 1.0 + target;
        if (fabs(a - target) <= 1e-7 * scale || (hi - lo) <= 1e-12 * (1.0 + hi)) {
            *w_out = mid;
            *a_out = a;
            return STROKE_AREA_OK;
        }
        if (a < target) lo = mid; else hi = mid;
        if (it == SA_MAX_ITER - 1) { *w_out = mid; *a_out = a; }
    }
    return STROKE_AREA_E_CONVERGE;
}

int stroke_area_fit(const SPoint *pts, int npts, int closed, double target,
                    double w_min, double w_max, SAreaFit *out) {
    if (!out) return STROKE_AREA_E_ARG;
    memset(out, 0, sizeof(*out));
    out->target = target;
    out->self_seg_a = -1;
    out->self_seg_b = -1;
    out->model_width = -1.0;
    out->model_area = -1.0;

    if (!pts || npts < 2 || npts > STROKE_AREA_MAX_PTS) return STROKE_AREA_E_ARG;
    for (int i = 0; i < npts; ++i)
        if (!isfinite(pts[i].x) || !isfinite(pts[i].y)) return STROKE_AREA_E_ARG;
    if (!isfinite(target) || target <= 0.0) return STROKE_AREA_E_TARGET;
    if (!isfinite(w_min) || !isfinite(w_max) || w_min < 0.0 || w_max <= w_min)
        return STROKE_AREA_E_WIDTH;

    double t0 = now_ms();
    SPoint p[STROKE_AREA_MAX_PTS];
    SGeom g;
    int rc = geom_for_measure(pts, npts, closed, p, &g);
    if (rc != STROKE_AREA_OK) { out->ms = now_ms() - t0; return rc; }
    out->length = g.length;
    out->quad = g.quad;
    out->closed = closed;

    /* 自交单独处理：标记段对与交点，宽度一律走实测（模型假设不自交）。 */
    SPoint at = {0, 0};
    int ia = -1, ib = -1;
    if (find_self(p, &g, &ia, &ib, &at)) {
        out->self_intersecting = 1;
        out->self_seg_a = ia;
        out->self_seg_b = ib;
        out->self_x = at.x;
        out->self_y = at.y;
    }

    /* 二次模型直接反解：L·w + k·w² = target。
     * k = 0 退化为线性；k < 0（闭合路径常见）取较小的正根。 */
    double w_model = -1.0;
    if (!g.fold) {
        double a2 = g.quad, b1 = g.length;
        double r = -1.0;
        if (fabs(a2) <= SA_EPS) {
            r = target / b1;
        } else {
            double disc = b1 * b1 + 4.0 * a2 * target;
            if (disc >= 0.0) {
                double sq = sqrt(disc);
                double r1 = (-b1 + sq) / (2.0 * a2);
                double r2 = (-b1 - sq) / (2.0 * a2);
                /* 取正的、且落在允许范围内的那一个 */
                if (r1 > 0.0 && isfinite(r1)) r = r1;
                if (r2 > 0.0 && isfinite(r2) && (r < 0.0 || r2 < r)) r = r2;
            }
        }
        if (r > 0.0 && isfinite(r) && r >= w_min && r <= w_max) w_model = r;
    }

    if (w_model > 0.0) {
        double a = stroke_area_measure(p, g.n, closed, w_model, 0.0);
        if (a > 0.0 && !out->self_intersecting &&
            fabs(a - target) <= SA_MODEL_TOL * target) {
            out->width = w_model;
            out->coverage = a;
            out->model_width = w_model;
            out->model_area = model_area(&g, w_model);
            out->rel_err = fabs(a - target) / target;
            out->ms = now_ms() - t0;
            return STROKE_AREA_OK;
        }
    }

    /* 实测反解 */
    out->used_measurement = 1;
    int iters = 0;
    double aw = 0.0, aa = 0.0;
    rc = solve_measured_real(p, g.n, closed, target, w_min, w_max,
                             0.0, &aw, &aa, &iters);
    out->iterations = iters;
    if (rc != STROKE_AREA_OK) { out->ms = now_ms() - t0; return rc; }
    /* 用更细的精度复测一次，报告值不依赖二分过程里的粗算。 */
    double fine = stroke_area_measure(p, g.n, closed, aw, 1e-10);
    if (fine > 0.0) aa = fine;
    out->width = aw;
    out->coverage = aa;
    out->rel_err = fabs(aa - target) / target;
    out->ms = now_ms() - t0;
    return STROKE_AREA_OK;
}

double stroke_area_model(const SPoint *pts, int npts, int closed, double width,
                         double *length, double *quad) {
    if (!pts || npts < 2 || npts > STROKE_AREA_MAX_PTS) return -1.0;
    if (!(width >= 0.0) || !isfinite(width)) return -1.0;
    SPoint p[STROKE_AREA_MAX_PTS];
    SGeom g;
    if (geom_for_measure(pts, npts, closed, p, &g) != STROKE_AREA_OK) return -1.0;
    if (g.fold) return -1.0;
    if (length) *length = g.length;
    if (quad) *quad = g.quad;
    return model_area(&g, width);
}

#endif /* STROKE_AREA_FIT */
