#define _POSIX_C_SOURCE 199309L
#include "simplify.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

const char *sp_strerror(int rc) {
    switch (rc) {
    case SP_OK:       return "ok";
    case SP_ERR_ARG:  return "参数错误：空指针";
    case SP_ERR_NPTS: return "拒绝：点不足二";
    case SP_ERR_SEG0: return "拒绝：段数为零";
    case SP_ERR_SEGN: return "拒绝：段数超过 n-1，无法每段至少两点";
    case SP_ERR_DEV:  return "拒绝：偏差上界为负";
    case SP_ERR_BIG:  return "拒绝：点数超过上限";
    case SP_ERR_MEM:  return "拒绝：内存分配失败";
    default:          return "未知错误";
    }
}

/* 点到线段距离 */
static double pt_seg_dist(SPoint p, SPoint a, SPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 < 1e-24) {
        double ex = p.x - a.x, ey = p.y - a.y;
        return sqrt(ex * ex + ey * ey);
    }
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    double qx = a.x + t * dx - p.x, qy = a.y + t * dy - p.y;
    return sqrt(qx * qx + qy * qy);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

/* ---------- 旧逻辑：全局 RDP ---------- */

int sp_simplify_global(const SPoint *pts, int n, double eps, SPoly *out) {
    if (!pts || !out) return SP_ERR_ARG;
    if (n < 2) return SP_ERR_NPTS;
    if (n > SP_MAX_PTS) return SP_ERR_BIG;
    if (eps < 0.0) return SP_ERR_DEV;

    unsigned char keep[SP_MAX_PTS];
    int stack_a[SP_MAX_PTS], stack_b[SP_MAX_PTS], top = 0;
    memset(keep, 0, (size_t)n);
    keep[0] = keep[n - 1] = 1;
    stack_a[top] = 0; stack_b[top] = n - 1; ++top;
    while (top > 0) {
        --top;
        int a = stack_a[top], b = stack_b[top];
        double best = -1.0;
        int bi = -1;
        for (int i = a + 1; i < b; ++i) {
            double d = pt_seg_dist(pts[i], pts[a], pts[b]);
            if (d > best) { best = d; bi = i; } /* 平局取小下标 */
        }
        if (bi >= 0 && best > eps) {
            keep[bi] = 1;
            stack_a[top] = a; stack_b[top] = bi; ++top;
            stack_a[top] = bi; stack_b[top] = b; ++top;
        }
    }
    out->count = 0;
    for (int i = 0; i < n; ++i) {
        if (keep[i]) {
            out->pts[out->count] = pts[i];
            out->idx[out->count] = i;
            ++out->count;
        }
    }
    return SP_OK;
}

/* ---------- 新逻辑：分段 + DP ---------- */

/* 按累计弧长等分，边界吸附到最近点下标（平局取小），再强制严格递增。
 * 完全确定，多次运行边界一致。 */
static void make_boundaries(const SPoint *pts, int n, int seg, int *bnd) {
    double s[SP_MAX_PTS];
    s[0] = 0.0;
    for (int i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i - 1].x, dy = pts[i].y - pts[i - 1].y;
        s[i] = s[i - 1] + sqrt(dx * dx + dy * dy);
    }
    double total = s[n - 1];
    bnd[0] = 0;
    bnd[seg] = n - 1;
    for (int k = 1; k < seg; ++k) {
        if (total < 1e-12) {
            bnd[k] = (int)((long)k * (n - 1) / seg); /* 退化路径按下标等分 */
            continue;
        }
        double target = total * k / seg;
        int bi = 0;
        double bd = fabs(s[0] - target);
        for (int i = 1; i < n; ++i) {
            double d = fabs(s[i] - target);
            if (d < bd) { bd = d; bi = i; }
        }
        bnd[k] = bi;
    }
    for (int k = 1; k <= seg; ++k) {
        int lo = bnd[k - 1] + 1, hi = n - 1 - (seg - k);
        if (bnd[k] < lo) bnd[k] = lo;
        if (bnd[k] > hi) bnd[k] = hi;
    }
}

/* 段 [a,b] 的“最远点优先”保留序列与代价表。
 * cost[m]：保留 m 个点时，被弃点到相邻保留点连线的最大距离。
 * order[j]：第 j 个加入的内点（段内局部下标），端点先于内点。 */
static void seg_cost(const SPoint *pts, int a, int b, double *cost, int *order) {
    int len = b - a + 1;
    unsigned char kept[SP_MAX_PTS];
    double dist[SP_MAX_PTS];
    memset(kept, 0, (size_t)len);
    kept[0] = kept[len - 1] = 1;
    for (int i = 1; i < len - 1; ++i)
        dist[i] = pt_seg_dist(pts[a + i], pts[a], pts[b]);
    for (int m = 2; m <= len; ++m) {
        double best = -1.0;
        int bi = -1;
        for (int i = 1; i < len - 1; ++i) {
            if (!kept[i] && dist[i] > best) { best = dist[i]; bi = i; }
        }
        cost[m] = (bi >= 0) ? best : 0.0;
        if (bi >= 0) {
            kept[bi] = 1;
            order[m - 2] = bi;
            /* 新保留点把两侧区间各分成两段，被弃点改测到相邻保留点连线的距离 */
            int lo = bi - 1;
            while (!kept[lo]) --lo;
            int hi = bi + 1;
            while (!kept[hi]) ++hi;
            for (int p = lo + 1; p < bi; ++p)
                dist[p] = pt_seg_dist(pts[a + p], pts[a + lo], pts[a + bi]);
            for (int q = bi + 1; q < hi; ++q)
                dist[q] = pt_seg_dist(pts[a + q], pts[a + bi], pts[a + hi]);
        }
    }
}

static int simplify_dp(const SPoint *pts, int n, int seg, double max_dev,
                       SPoly *out, SStats *st) {
    int bnd[SP_MAX_SEG + 1];
    make_boundaries(pts, n, seg, bnd);
    memcpy(st->boundary, bnd, sizeof(int) * (size_t)(seg + 1));

    int umax = n - seg - 1; /* 全部内点都保留时的额外点数总量 */
    double *cost = malloc(sizeof(double) * (size_t)seg * (size_t)(n + 1));
    int *order = malloc(sizeof(int) * (size_t)seg * (size_t)n);
    double *f = malloc(sizeof(double) * (size_t)(seg + 1) * (size_t)(umax + 1));
    int *par = malloc(sizeof(int) * (size_t)(seg + 1) * (size_t)(umax + 1));
    if (!cost || !order || !f || !par) {
        free(cost); free(order); free(f); free(par);
        return SP_ERR_MEM;
    }

    for (int k = 0; k < seg; ++k)
        seg_cost(pts, bnd[k], bnd[k + 1],
                 cost + (size_t)k * (n + 1), order + (size_t)k * n);

    /* f[k][u]：前 k 段共用 u 个额外内点时的最小总偏差 */
    const double INF = 1e300;
    for (int k = 0; k <= seg; ++k)
        for (int u = 0; u <= umax; ++u) {
            f[(size_t)k * (umax + 1) + u] = INF;
            par[(size_t)k * (umax + 1) + u] = -1;
        }
    f[0] = 0.0;
    for (int k = 1; k <= seg; ++k) {
        int len = bnd[k] - bnd[k - 1] + 1;
        int uk_max = len - 2;
        for (int u = 0; u <= umax; ++u) {
            for (int uk = 0; uk <= uk_max && uk <= u; ++uk) {
                double prev = f[(size_t)(k - 1) * (umax + 1) + (u - uk)];
                if (prev >= INF) continue;
                double v = prev + cost[(size_t)(k - 1) * (n + 1) + (2 + uk)];
                if (v < f[(size_t)k * (umax + 1) + u]) {
                    f[(size_t)k * (umax + 1) + u] = v;
                    par[(size_t)k * (umax + 1) + u] = uk;
                }
            }
        }
    }

    /* 最小保留点数使总偏差 <= max_dev；u 越大偏差越小，全保留时偏差为 0，必有解 */
    int u_best = -1;
    for (int u = 0; u <= umax; ++u) {
        if (f[(size_t)seg * (umax + 1) + u] <= max_dev + 1e-12) { u_best = u; break; }
    }
    if (u_best < 0) u_best = umax; /* 浮点兜底 */
    double total_dev = f[(size_t)seg * (umax + 1) + u_best];

    /* 回溯各段保留的内点数 */
    int uk_of[SP_MAX_SEG];
    int u = u_best;
    for (int k = seg; k >= 1; --k) {
        int uk = par[(size_t)k * (umax + 1) + u];
        if (uk < 0) uk = 0;
        uk_of[k - 1] = uk;
        u -= uk;
    }

    /* 按段重建保留点：段内取端点 + order 前 uk 个内点，按下标升序输出 */
    unsigned char keep[SP_MAX_PTS];
    memset(keep, 0, (size_t)n);
    for (int k = 0; k < seg; ++k) {
        int a = bnd[k], b = bnd[k + 1];
        keep[a] = keep[b] = 1;
        const int *ord = order + (size_t)k * n;
        for (int j = 0; j < uk_of[k]; ++j)
            keep[a + ord[j]] = 1;
    }
    out->count = 0;
    for (int i = 0; i < n; ++i) {
        if (keep[i]) {
            out->pts[out->count] = pts[i];
            out->idx[out->count] = i;
            ++out->count;
        }
    }

    st->segments = seg;
    st->kept = out->count;
    st->total_dev = total_dev;

    free(cost); free(order); free(f); free(par);
    return SP_OK;
}

int sp_simplify(const SPoint *pts, int n, const SOpts *opt, SPoly *out, SStats *st) {
    if (!pts || !opt || !out) return SP_ERR_ARG;
    if (n < 2) return SP_ERR_NPTS;
    if (n > SP_MAX_PTS) return SP_ERR_BIG;

    if (!opt->use_dp) {
        int rc = sp_simplify_global(pts, n, opt->eps, out); /* 与旧逻辑逐字节一致 */
        if (rc == SP_OK && st) {
            memset(st, 0, sizeof(*st));
            st->segments = 0;
            st->kept = out->count;
            st->total_dev = 0.0;
            st->elapsed_ms = 0.0;
        }
        return rc;
    }

    if (opt->segments == 0) return SP_ERR_SEG0;
    if (opt->segments < 0 || opt->segments > n - 1 || opt->segments > SP_MAX_SEG)
        return SP_ERR_SEGN;
    if (opt->max_dev < 0.0) return SP_ERR_DEV;

    double t0 = now_ms();
    SStats tmp;
    if (!st) st = &tmp;
    memset(st, 0, sizeof(*st));
    int rc = simplify_dp(pts, n, opt->segments, opt->max_dev, out, st);
    st->elapsed_ms = now_ms() - t0;
    return rc;
}
