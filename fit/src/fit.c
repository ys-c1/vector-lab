#include "fit.h"
#include <math.h>
#include <stddef.h>

static double fdist(FPoint a, FPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

static FPoint feval(const FPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    FPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

/* 弦长参数化：t[0]=0, t[n-1]=1。返回 -1 表示总弦长为 0。 */
static int parameterize(const FPoint *p, int n, double *t) {
    double total = 0.0;
    t[0] = 0.0;
    for (int i = 1; i < n; ++i) {
        total += fdist(p[i - 1], p[i]);
        t[i] = total;
    }
    if (total <= 0.0) return -1;
    for (int i = 1; i < n; ++i) t[i] /= total;
    return 0;
}

/* 固定端点 P0=pts[0], P3=pts[n-1]，最小二乘解内部控制点 P1,P2。
 * 法方程为 2x2（x、y 共用同一系数矩阵）。 */
static void solve_inner(const FPoint *p, int n, const double *t,
                        FPoint *p1, FPoint *p2) {
    FPoint p0 = p[0], p3 = p[n - 1];
    double a11 = 0.0, a12 = 0.0, a22 = 0.0;
    double c1x = 0.0, c1y = 0.0, c2x = 0.0, c2y = 0.0;
    for (int i = 0; i < n; ++i) {
        double u = 1.0 - t[i];
        double b0 = u * u * u, b1 = 3 * u * u * t[i];
        double b2 = 3 * u * t[i] * t[i], b3 = t[i] * t[i] * t[i];
        double rx = p[i].x - b0 * p0.x - b3 * p3.x;
        double ry = p[i].y - b0 * p0.y - b3 * p3.y;
        a11 += b1 * b1;
        a12 += b1 * b2;
        a22 += b2 * b2;
        c1x += b1 * rx; c1y += b1 * ry;
        c2x += b2 * rx; c2y += b2 * ry;
    }
    double det = a11 * a22 - a12 * a12;
    if (det <= 1e-12 * (a11 * a22 + 1e-30)) {
        /* 法方程奇异：退化为弦的三分点，保证有定义。 */
        p1->x = p0.x + (p3.x - p0.x) / 3.0;
        p1->y = p0.y + (p3.y - p0.y) / 3.0;
        p2->x = p0.x + (p3.x - p0.x) * 2.0 / 3.0;
        p2->y = p0.y + (p3.y - p0.y) * 2.0 / 3.0;
        return;
    }
    p1->x = (c1x * a22 - c2x * a12) / det;
    p1->y = (c1y * a22 - c2y * a12) / det;
    p2->x = (a11 * c2x - a12 * c1x) / det;
    p2->y = (a11 * c2y - a12 * c1y) / det;
}

/* 对每个必经点做一次牛顿重参数化，找曲线上最近点的参数。 */
static void reparameterize(const FPoint *p, int n, const FPoint c[4], double *t) {
    for (int i = 1; i < n - 1; ++i) {
        double ti = t[i];
        for (int it = 0; it < 2; ++it) {
            FPoint b = feval(c, ti);
            double u = 1.0 - ti;
            /* B'(t) 与 B''(t) */
            double d1x = 3 * u * u * (c[1].x - c[0].x)
                       + 6 * u * ti * (c[2].x - c[1].x)
                       + 3 * ti * ti * (c[3].x - c[2].x);
            double d1y = 3 * u * u * (c[1].y - c[0].y)
                       + 6 * u * ti * (c[2].y - c[1].y)
                       + 3 * ti * ti * (c[3].y - c[2].y);
            double d2x = 6 * u * (c[2].x - 2 * c[1].x + c[0].x)
                       + 6 * ti * (c[3].x - 2 * c[2].x + c[1].x);
            double d2y = 6 * u * (c[2].y - 2 * c[1].y + c[0].y)
                       + 6 * ti * (c[3].y - 2 * c[2].y + c[1].y);
            double dx = b.x - p[i].x, dy = b.y - p[i].y;
            double num = dx * d1x + dy * d1y;
            double den = d1x * d1x + d1y * d1y + dx * d2x + dy * d2y;
            if (fabs(den) < 1e-18) break;
            ti -= num / den;
            if (ti < 0.0) ti = 0.0;
            if (ti > 1.0) ti = 1.0;
        }
        t[i] = ti;
    }
}

static double max_residual(const FPoint *p, int n, const double *t,
                           const FPoint c[4], int *argmax) {
    double m = 0.0;
    int mi = 0;
    for (int i = 0; i < n; ++i) {
        double d = fdist(feval(c, t[i]), p[i]);
        if (d > m) { m = d; mi = i; }
    }
    if (argmax) *argmax = mi;
    return m;
}

/* 单段拟合核心；t_out 非空时回传收敛后的参数化（供分段定位最差点）。 */
static int fit_core(const FPoint *pts, int n, FFit *out, double *t_out) {
    if (!pts || !out || n > FIT_MAX) return -2;
    if (n < 2) return -1;

    FPoint p0 = pts[0], p3 = pts[n - 1];
    double chord = fdist(p0, p3);

    /* 退化 2：所有点重合（散布为零）。 */
    double spread = 0.0;
    for (int i = 0; i < n; ++i) {
        double d = fdist(p0, pts[i]);
        if (d > spread) spread = d;
    }
    if (spread <= 1e-9) {
        for (int k = 0; k < 4; ++k) out->ctrl[k] = p0;
        out->max_err = 0.0;
        out->degenerate = 2;
        return 0;
    }

    /* 退化 1：共线（含 n==2）。控制点取弦的三分点，曲线即直线。 */
    if (chord > 1e-12) {
        double maxd = 0.0;
        for (int i = 1; i < n - 1; ++i) {
            double cross = (p3.x - p0.x) * (pts[i].y - p0.y)
                         - (p3.y - p0.y) * (pts[i].x - p0.x);
            double d = fabs(cross) / chord;
            if (d > maxd) maxd = d;
        }
        if (maxd <= 1e-9 * chord) {
            out->ctrl[0] = p0;
            out->ctrl[3] = p3;
            out->ctrl[1].x = p0.x + (p3.x - p0.x) / 3.0;
            out->ctrl[1].y = p0.y + (p3.y - p0.y) / 3.0;
            out->ctrl[2].x = p0.x + (p3.x - p0.x) * 2.0 / 3.0;
            out->ctrl[2].y = p0.y + (p3.y - p0.y) * 2.0 / 3.0;
            out->max_err = maxd;
            out->degenerate = 1;
            return 0;
        }
    }

    /* 一般情形：弦长参数化 + 最小二乘。 */
    double t[FIT_MAX];
    if (parameterize(pts, n, t) != 0) return -2;
    out->ctrl[0] = p0;
    out->ctrl[3] = p3;
    solve_inner(pts, n, t, &out->ctrl[1], &out->ctrl[2]);
    /* 交替「重参数化 + 重解」迭代，残差收敛或到上限即停。
     * 每轮 O(n)，64 轮对交互场景仍是微秒级。 */
    double prev = max_residual(pts, n, t, out->ctrl, NULL);
    for (int round = 0; round < 64; ++round) {
        reparameterize(pts, n, out->ctrl, t);
        solve_inner(pts, n, t, &out->ctrl[1], &out->ctrl[2]);
        double err = max_residual(pts, n, t, out->ctrl, NULL);
        if (err >= prev - 1e-12) { prev = err < prev ? err : prev; break; }
        prev = err;
    }
    out->max_err = prev;
    out->degenerate = 0;
    if (t_out) {
        for (int i = 0; i < n; ++i) t_out[i] = t[i];
    }
    return 0;
}

int fit_bezier(const FPoint *pts, int n, FFit *out) {
    return fit_core(pts, n, out, NULL);
}

/* 递归分段：单段超差时在最差必经点处切开，两半分别拟合。 */
static int fit_split(const FPoint *pts, int n, double tol,
                     FFit *segs, int max_segs, int *used) {
    FFit f;
    double t[FIT_MAX];
    int rc = fit_core(pts, n, &f, t);
    if (rc != 0) return rc;
    if (f.max_err <= tol || n <= 2 || f.degenerate != 0) {
        if (*used >= max_segs) return -3;
        segs[(*used)++] = f;
        return 0;
    }
    /* 找残差最大的必经点作为切分点（端点残差恒为 0，必为内部点）。 */
    int mid = n / 2;
    max_residual(pts, n, t, f.ctrl, &mid);
    if (mid <= 0 || mid >= n - 1) mid = n / 2;
    rc = fit_split(pts, mid + 1, tol, segs, max_segs, used);
    if (rc != 0) return rc;
    return fit_split(pts + mid, n - mid, tol, segs, max_segs, used);
}

int fit_spline(const FPoint *pts, int n, double tol, FFit *segs, int max_segs) {
    if (!pts || !segs || n > FIT_MAX || max_segs < 1 || tol <= 0.0) return -2;
    if (n < 2) return -1;
    int used = 0;
    int rc = fit_split(pts, n, tol, segs, max_segs, &used);
    if (rc != 0) return rc;
    return used;
}
