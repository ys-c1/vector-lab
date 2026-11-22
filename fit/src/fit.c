#include "fit.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double fmin2(double a, double b) { return a < b ? a : b; }
static double fmax2(double a, double b) { return a > b ? a : b; }

static FPoint rot(FPoint p, double c, double s) {
    FPoint r;
    r.x = p.x * c - p.y * s;
    r.y = p.x * s + p.y * c;
    return r;
}

/* 指定角度下旋转点集的轴对齐包围盒；返回面积，宽高经 *w *h 带出。 */
static double box_at(const FPoint *pts, int n, double angle, double *w, double *h) {
    double c = cos(angle), s = sin(angle);
    double minx = 0.0, maxx = 0.0, miny = 0.0, maxy = 0.0;
    for (int i = 0; i < n; ++i) {
        FPoint q = rot(pts[i], c, s);
        if (i == 0) { minx = maxx = q.x; miny = maxy = q.y; }
        else {
            minx = fmin2(minx, q.x); maxx = fmax2(maxx, q.x);
            miny = fmin2(miny, q.y); maxy = fmax2(maxy, q.y);
        }
    }
    *w = maxx - minx;
    *h = maxy - miny;
    return (*w) * (*h);
}

double fit_area_at(const FPoint *pts, int n, double angle) {
    if (!pts || n < 1) return -1.0;
    double w, h;
    return box_at(pts, n, angle, &w, &h);
}

int fit_validate(double angle) {
    if (!(angle >= 0.0 && angle < M_PI / 2.0)) return -1;
    return 0;
}

static int cmp(const void *a, const void *b) {
    const FPoint *p = (const FPoint*)a, *q = (const FPoint*)b;
    if (p->x < q->x) return -1;
    if (p->x > q->x) return 1;
    if (p->y < q->y) return -1;
    if (p->y > q->y) return 1;
    return 0;
}

static double cross(FPoint o, FPoint a, FPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/* Andrew 单调链凸包，共线点剔除（cross <= 0 出栈），输出逆时针顶点。 */
static int convex_hull(FPoint *s, int n, FPoint *out) {
    qsort(s, (size_t)n, sizeof(FPoint), cmp);
    if (n == 1) { out[0] = s[0]; return 1; }
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(out[k-2], out[k-1], s[i]) <= 0.0) --k;
        out[k++] = s[i];
    }
    int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && cross(out[k-2], out[k-1], s[i]) <= 0.0) --k;
        out[k++] = s[i];
    }
    return k - 1;
}

/* 把任意方向角折叠到一个周期 [0, pi/2) 内。 */
static double norm_half_pi(double a) {
    double p = M_PI / 2.0;
    a = fmod(a, p);
    if (a < 0.0) a += p;
    if (a >= p - FIT_EPS) a = 0.0;   /* 数值误差贴到周期边界时归零 */
    return a;
}

int fit_optimize(const FPoint *pts, int n, FitResult *out) {
    if (!pts || !out || n < 1) return -1;

    double w0, h0;
    double a0 = box_at(pts, n, 0.0, &w0, &h0);
    out->area0 = a0;

    /* 退化场景（点或共线，零度面积为 0）：无优化空间。 */
    if (a0 <= FIT_EPS) {
        out->angle = 0.0; out->area = a0; out->w = w0; out->h = h0;
        out->indifferent = 1;
        return 0;
    }

    FPoint *s = (FPoint*)malloc(sizeof(FPoint) * (size_t)n);
    if (!s) return -2;
    for (int i = 0; i < n; ++i) s[i] = pts[i];
    FPoint *hull = (FPoint*)malloc(sizeof(FPoint) * (size_t)(n + 1));
    if (!hull) { free(s); return -2; }
    int m = convex_hull(s, n, hull);

    /*
     * 旋转卡尺定理：A(theta) 的最小点必落在某条凸包边与坐标轴平行的方向上，
     * 即候选角只有各边的方向角（折叠到 [0, pi/2)），外加 0 本身。
     */
    double best = a0, bestAng = 0.0, bestW = w0, bestH = h0;
    for (int i = 0; i < m; ++i) {
        FPoint e = hull[(i + 1) % m];
        /* 边方向为 alpha 时，需旋转 -alpha 才能与包围盒轴平行；再折叠进 [0, pi/2)。 */
        double ang = norm_half_pi(-atan2(e.y - hull[i].y, e.x - hull[i].x));
        if (ang < FIT_EPS) continue;
        double w, h;
        double area = box_at(pts, n, ang, &w, &h);
        if (area < best - FIT_EPS) {       /* 严格更优才换，平手保留零角 */
            best = area; bestAng = ang; bestW = w; bestH = h;
        }
    }
    free(hull);
    free(s);

    out->angle = bestAng;
    out->area = best;
    out->w = bestW;
    out->h = bestH;
    /* 无差别场景：最优面积相对零度改善不足 1e-9（正方形、正多边形等）。 */
    out->indifferent = (a0 - best) <= FIT_EPS * fmax2(1.0, a0);
    return 0;
}
