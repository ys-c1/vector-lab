#include "adapt.h"
#include <math.h>
#include <stdlib.h>

#define SAFETY 0.9          /* 反解间隔安全系数，给离散化估计与舍入留余量 */
#define ADAPT_MAX_NODES 200000

static double seg_len(APoint a, APoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

/* 在原始折线上按弧长 s 取真实点（验证基准）。
 * 端点处允许 1e-12 相对量级的浮点越界并夹紧，其余越界报错。 */
static int curve_point_at(const APoint *pts, int n, const double *cum,
                          double total, double s, APoint *out) {
    if (s < 0.0) return -1;
    if (s > total) {
        if (s <= total + fabs(total) * 1e-12) s = total;
        else return -1;
    }
    if (s >= total) { *out = pts[n - 1]; return 0; }
    int i = 0;
    while (i + 2 < n && cum[i + 1] < s) ++i;
    double span = cum[i + 1] - cum[i];
    double f = span > 1e-15 ? (s - cum[i]) / span : 0.0;
    out->x = pts[i].x + (pts[i + 1].x - pts[i].x) * f;
    out->y = pts[i].y + (pts[i + 1].y - pts[i].y) * f;
    return 0;
}

int adapt_build(const APoint *pts, int n, const AdaptConfig *cfg, AdaptiveTable *out) {
    if (!pts || !cfg || !out || n < 2 || cfg->eps <= 0.0) return -1;

    double *cum = (double *)malloc((size_t)n * sizeof(double));
    if (!cum) return -3;
    cum[0] = 0.0;
    for (int i = 1; i < n; ++i)
        cum[i] = cum[i - 1] + seg_len(pts[i - 1], pts[i]);
    double total = cum[n - 1];
    if (!(total > 0.0) || !isfinite(total)) { free(cum); return -1; }

    /* 最大曲率：连续三点的 Menger 曲率 k = 2|cross|/(a*b*c)。
     * 近退化三元组（分母相对曲线尺度趋零）无法稳定估计，跳过。 */
    double scale3 = total * total * total;
    double kmax = 0.0;
    for (int i = 1; i + 1 < n; ++i) {
        double ux = pts[i].x - pts[i - 1].x, uy = pts[i].y - pts[i - 1].y;
        double vx = pts[i + 1].x - pts[i].x, vy = pts[i + 1].y - pts[i].y;
        double cross = fabs(ux * vy - uy * vx);
        double a = hypot(ux, uy);
        double c = hypot(vx, vy);
        double b = seg_len(pts[i - 1], pts[i + 1]);
        double den = a * b * c;
        if (den <= 1e-15 * scale3) continue;   /* 近退化，跳过 */
        double k = 2.0 * cross / den;
        if (isfinite(k) && k > kmax) kmax = k;
    }

    /* 反解通用矢高上界：对底边长 c 的三元组，由 4A=abc*k 与三角形面积
     * A <= c*h/2（底*高/2）得 h(d) <= k*d^2/2（圆弧恰取等），
     * 故 d <= sqrt(2 eps / k)。 */
    double d;
    if (kmax <= 0.0) {
        d = total;                  /* 直线：单弦精确，两节点即可 */
    } else {
        d = SAFETY * sqrt(2.0 * cfg->eps / kmax);
        if (!isfinite(d) || d <= 0.0) { free(cum); return -1; }
        /* 间隔大于总长：单弦（端点直连）的矢高误差已超上界，
         * 任何非空等间隔表都无法满足，拒绝建表。 */
        if (d > total) { free(cum); return -2; }
        /* 要求间隔过小、表长超出硬上限，按参数不可满足报错。 */
        if (d < total / (ADAPT_MAX_NODES - 1)) { free(cum); return -1; }
    }

    int count = (int)floor(total / d) + 1;
    if (count < 2) { free(cum); return -1; }      /* 表长为零/一，报错 */
    if (count > ADAPT_MAX_NODES) { free(cum); return -1; }

    double *ts = (double *)malloc((size_t)count * sizeof(double));
    double *tx = (double *)malloc((size_t)count * sizeof(double));
    double *ty = (double *)malloc((size_t)count * sizeof(double));
    if (!ts || !tx || !ty) { free(cum); free(ts); free(tx); free(ty); return -3; }

    for (int i = 0; i < count; ++i) {
        double s = fmin(d * i, total);
        if (i == count - 1) s = total;            /* 末节点强制落到终点 */
        APoint q;
        curve_point_at(pts, n, cum, total, s, &q);
        ts[i] = s; tx[i] = q.x; ty[i] = q.y;
    }

    free(cum);
    out->count = count;
    out->eps = cfg->eps;
    out->max_interval = d;
    out->total = total;
    out->kmax = kmax;
    out->s = ts; out->x = tx; out->y = ty;
    return 0;
}

int adapt_sample(const AdaptiveTable *t, double s, APoint *out) {
    if (!t || !out || t->count < 2 || !t->s) return -1;
    if (s < 0.0) return -2;
    if (s > t->total) {
        if (s <= t->total + fabs(t->total) * 1e-12) s = t->total;   /* 端点舍入夹紧 */
        else return -2;
    }
    int i = 0;
    while (i + 2 < t->count && t->s[i + 1] < s) ++i;
    double span = t->s[i + 1] - t->s[i];
    double f = span > 1e-15 ? (s - t->s[i]) / span : 0.0;
    out->x = t->x[i] + (t->x[i + 1] - t->x[i]) * f;
    out->y = t->y[i] + (t->y[i + 1] - t->y[i]) * f;
    return 0;
}

double adapt_verify(const APoint *pts, int n, const AdaptiveTable *t, int probes) {
    if (!pts || !t || n < 2 || probes < 2 || t->count < 2) return -1.0;
    double *cum = (double *)malloc((size_t)n * sizeof(double));
    if (!cum) return -3.0;
    cum[0] = 0.0;
    for (int i = 1; i < n; ++i)
        cum[i] = cum[i - 1] + seg_len(pts[i - 1], pts[i]);
    double total = cum[n - 1];

    double worst = 0.0;
    for (int j = 0; j < probes; ++j) {
        double s = total * (double)j / (double)(probes - 1);
        APoint truth, approx;
        if (curve_point_at(pts, n, cum, total, s, &truth) != 0) { free(cum); return -1.0; }
        if (adapt_sample(t, s, &approx) != 0) { free(cum); return -1.0; }
        double e = hypot(approx.x - truth.x, approx.y - truth.y);
        if (e > worst) worst = e;
    }
    free(cum);
    return worst;
}

void adapt_free(AdaptiveTable *t) {
    if (!t) return;
    free(t->s); free(t->x); free(t->y);
    t->s = NULL; t->x = NULL; t->y = NULL;
    t->count = 0;
}
