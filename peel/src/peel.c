#define _POSIX_C_SOURCE 200809L
#include "peel.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 判定“点落在凸包边上”的容差：取点集包围盒对角线长度的 1e-9。
 * 输入坐标来自十进制文本，量化误差约 1e-6；1e-9 倍对角线对任何
 * 有意义的图形都远大于量化误差，又远小于真实几何间距。 */
static double on_edge_eps(double diag) {
    double e = 1e-9 * diag;
    return e > 1e-12 ? e : 1e-12;
}

static int pt_less(const void *a, const void *b) {
    const PPoint *p = (const PPoint *)a, *q = (const PPoint *)b;
    if (p->x < q->x) return -1;
    if (p->x > q->x) return 1;
    if (p->y < q->y) return -1;
    if (p->y > q->y) return 1;
    return 0;
}

static int pt_eq(PPoint a, PPoint b) {
    return a.x == b.x && a.y == b.y;
}

static double cross(PPoint o, PPoint a, PPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/* 点到线段的最短距离（不是到无限直线的距离）。 */
static double pt_seg_dist(PPoint a, PPoint b, PPoint p) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double L2 = dx * dx + dy * dy;
    double t = 0.0;
    if (L2 > 0.0) {
        t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / L2;
        if (t < 0.0) t = 0.0;
        if (t > 1.0) t = 1.0;
    }
    double qx = a.x + dx * t, qy = a.y + dy * t;
    double ex = p.x - qx, ey = p.y - qy;
    return sqrt(ex * ex + ey * ey);
}

/* 逆时针凸包（Andrew 单调链），严格顶点，共线点不入栈。
 * out 需至少 2*n+2 个点的容量；返回顶点数（0/1/2 为退化）。 */
static int convex_hull(const PPoint *s, int n, PPoint *out) {
    if (n <= 1) {
        if (n == 1) out[0] = s[0];
        return n;
    }
    PPoint *h = out;
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(h[k - 2], h[k - 1], s[i]) <= 0.0) --k;
        h[k++] = s[i];
    }
    int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && cross(h[k - 2], h[k - 1], s[i]) <= 0.0) --k;
        h[k++] = s[i];
    }
    return k - 1;
}

double peel_area(const PPoint *poly, int n) {
    if (!poly || n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        PPoint a = poly[i], b = poly[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return fabs(s) * 0.5;
}

static double ms_since(const struct timespec *t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ns = (double)(t1.tv_sec - t0->tv_sec) * 1e9 + (double)(t1.tv_nsec - t0->tv_nsec);
    return ns / 1e6;
}

/* 精确去重（就地排序后压缩），返回去重后点数。 */
static int dedup(PPoint *p, int n) {
    if (n <= 1) return n;
    qsort(p, (size_t)n, sizeof(PPoint), pt_less);
    int k = 1;
    for (int i = 1; i < n; ++i)
        if (!pt_eq(p[i], p[k - 1])) p[k++] = p[i];
    return k;
}

int peel_run(const PPoint *pts, int n, PPeelOut *out, PSummary *sum) {
    if (!out || !out->layers || !sum || n < 0 || n > PEEL_MAX_POINTS) return -1;
    if (n > 0 && !pts) return -1;
    for (int i = 0; i < n; ++i)
        if (!isfinite(pts[i].x) || !isfinite(pts[i].y)) return -1;

    memset(sum, 0, sizeof *sum);
    sum->input_points = n;
    sum->stop.layer = 1;
    sum->stop.reason = PEEL_STOP_DONE;

    if (n == 0) { sum->stop.reason = PEEL_STOP_FEW; return 3; }

    /* 三份工作缓冲：剩余点、排序副本/下一轮剩余点、凸包。 */
    size_t cap = (size_t)n;
    PPoint *rem = (PPoint *)malloc(sizeof(PPoint) * cap);
    PPoint *nxt = (PPoint *)malloc(sizeof(PPoint) * cap);
    PPoint *hull = (PPoint *)malloc(sizeof(PPoint) * (2 * cap + 2));
    if (!rem || !nxt || !hull) { free(rem); free(nxt); free(hull); return -1; }
    memcpy(rem, pts, sizeof(PPoint) * cap);
    int nr = dedup(rem, n);
    sum->unique_points = nr;

    double minx = rem[0].x, maxx = rem[0].x, miny = rem[0].y, maxy = rem[0].y;
    for (int i = 1; i < nr; ++i) {
        if (rem[i].x < minx) minx = rem[i].x;
        if (rem[i].x > maxx) maxx = rem[i].x;
        if (rem[i].y < miny) miny = rem[i].y;
        if (rem[i].y > maxy) maxy = rem[i].y;
    }
    double diag = sqrt((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny));
    double eps = on_edge_eps(diag);

    int nlayers = 0;
    double prev_area = 0.0;
    int rc = 0;

    while (nr > 0) {
        int layer_no = nlayers + 1;
        if (layer_no > out->layer_cap) {
            sum->stop.layer = layer_no;
            sum->stop.remaining = nr;
            sum->stop.reason = PEEL_STOP_CAPACITY;
            rc = -4;
            break;
        }
        if (nr < 3) {
            sum->stop.layer = layer_no;
            sum->stop.remaining = nr;
            sum->stop.reason = PEEL_STOP_FEW;
            if (nlayers == 0) rc = 3;
            break;
        }

        struct timespec t0;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        /* rem 已按 (x,y) 有序（去重时排过；每轮结束后 nxt 仍保持原序子集），
         * 凸包直接吃 rem，不需要再排序。 */
        int nh = convex_hull(rem, nr, hull);
        double area = nh >= 3 ? peel_area(hull, nh) : 0.0;
        if (nh < 3 || area <= 0.0) {
            sum->stop.layer = layer_no;
            sum->stop.remaining = nr;
            sum->stop.reason = PEEL_STOP_COLLINEAR;
            if (nlayers == 0) rc = 2;
            break;
        }
        if (nlayers > 0 && area > prev_area * (1.0 + 1e-12)) {
            sum->stop.layer = layer_no;
            sum->stop.remaining = nr;
            sum->stop.reason = PEEL_STOP_INTERNAL;
            rc = -5;
            break;
        }

        /* 移出落在凸包边界 eps 带上的点。 */
        int keep = 0;
        for (int i = 0; i < nr; ++i) {
            int on = 0;
            if (nh == 1) {
                on = pt_eq(rem[i], hull[0]);
            } else if (nh == 2) {
                on = pt_seg_dist(hull[0], hull[1], rem[i]) <= eps;
            } else {
                for (int e = 0; e < nh; ++e) {
                    if (pt_seg_dist(hull[e], hull[(e + 1) % nh], rem[i]) <= eps) { on = 1; break; }
                }
            }
            if (!on) nxt[keep++] = rem[i];
        }
        if (keep == nr) {
            sum->stop.layer = layer_no;
            sum->stop.remaining = nr;
            sum->stop.reason = PEEL_STOP_INTERNAL;
            rc = -5;
            break;
        }

        PLayer *L = &out->layers[nlayers];
        L->layer = layer_no;
        L->input_unique = nr;
        L->hull_verts = nh;
        L->removed = nr - keep;
        L->area = area;
        L->ms = ms_since(&t0);

        if (out->on_layer) {
            int stop = out->on_layer(out->ctx, rem, nr, hull, nh, L);
            if (stop != 0) {
                sum->stop.layer = layer_no;
                sum->stop.remaining = nr;
                sum->stop.reason = PEEL_STOP_INTERNAL;
                rc = stop;
                break;
            }
        }
        ++nlayers;

        PPoint *tmp = rem; rem = nxt; nxt = tmp;
        nr = keep;
        prev_area = area;
    }

    if (nr == 0 && rc == 0) {
        sum->stop.layer = nlayers + 1;
        sum->stop.remaining = 0;
        sum->stop.reason = PEEL_STOP_DONE;
    }
    sum->layer_count = nlayers;
    free(rem); free(nxt); free(hull);
    return rc;
}

int peel_cover_layer(const PLayer *layers, int nlayers, double frac, double *ratio) {
    if (!layers || nlayers <= 0) return -1;
    if (!(frac > 0.0)) { if (ratio) *ratio = 1.0; return 0; }
    double base = layers[0].area;
    if (!(base > 0.0)) return -1;
    if (frac >= 1.0) frac = 1.0;

    /* 剥离第 1..k 层后，剩余面积取第 k+1 层凸包面积；末层剥完剩余记 0。
     * 各层凸包嵌套（后层点集是前层子集），所以剩余占比单调不增，
     * 首个满足 剩余占比 <= 1-frac 的 k 就是答案。 */
    for (int k = 1; k <= nlayers; ++k) {
        double rest = (k < nlayers) ? layers[k].area : 0.0;
        double r = rest / base;
        if (r <= 1.0 - frac) {
            if (ratio) *ratio = r;
            return k;
        }
    }
    if (ratio) *ratio = 0.0;
    return -2;
}
