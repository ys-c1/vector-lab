#define _POSIX_C_SOURCE 199309L
#include "clip.h"
#include <time.h>

static int inside(CPoint a, CPoint b, CPoint p) {
    double cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return cr >= 0.0;
}

static CPoint meet(CPoint a, CPoint b, CPoint c, CPoint d) {
    double a1 = b.y - a.y, b1 = a.x - b.x, c1 = a1 * a.x + b1 * a.y;
    double a2 = d.y - c.y, b2 = c.x - d.x, c2 = a2 * c.x + b2 * c.y;
    double det = a1 * b2 - a2 * b1;
    CPoint r = {0, 0};
    if (det == 0.0) return r;
    r.x = (b2 * c1 - b1 * c2) / det;
    r.y = (a1 * c2 - a2 * c1) / det;
    return r;
}

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
    CPoly cur, nxt;
    cur.count = n;
    for (int i = 0; i < n; ++i) cur.pts[i] = poly[i];
    for (int e = 0; e < wn; ++e) {
        CPoint a = win[e], b = win[(e + 1) % wn];
        nxt.count = 0;
        for (int i = 0; i < cur.count; ++i) {
            CPoint p = cur.pts[i], q = cur.pts[(i + 1) % cur.count];
            int ip = inside(a, b, p), iq = inside(a, b, q);
            if (ip) {
                if (nxt.count < 62) nxt.pts[nxt.count++] = p;
            }
            if (ip != iq) {
                CPoint m = meet(p, q, a, b);
                if (nxt.count < 62) nxt.pts[nxt.count++] = m;
            }
        }
        cur = nxt;
        if (cur.count == 0) break;
    }
    out->count = cur.count;
    for (int i = 0; i < cur.count; ++i) out->pts[i] = cur.pts[i];
    return 0;
}

double clip_area(const CPoly *p) {
    if (!p || p->count < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < p->count; ++i) {
        CPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

#ifndef CLIP_NO_MULTI
/* ---------------- 多窗口顺序裁决 ---------------- */

size_t clip_factorial(int m) {
    size_t f = 1;
    for (int i = 2; i <= m; ++i) f *= (size_t)i;
    return f;
}

double clip_area_kahan(const CPoly *p) {
    if (!p || p->count < 3) return 0.0;
    double s = 0.0, c = 0.0;
    for (int i = 0; i < p->count; ++i) {
        CPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        double t = a.x * b.y - b.x * a.y;
        double y = t - c;
        double z = s + y;
        c = (z - s) - y;
        s = z;
    }
    return s * 0.5;
}

int clip_window_empty(const CPoly *w) {
    if (!w || w->count < 3) return 1;
    /* 退化判定：面积相对外接矩形面积可忽略 => 共线/重合点，无有效内部 */
    double area = clip_area_kahan(w);
    if (area < 0) area = -area;
    double minx = w->pts[0].x, maxx = minx, miny = w->pts[0].y, maxy = miny;
    for (int i = 1; i < w->count; ++i) {
        double x = w->pts[i].x, y = w->pts[i].y;
        if (x < minx) minx = x;
        if (x > maxx) maxx = x;
        if (y < miny) miny = y;
        if (y > maxy) maxy = y;
    }
    double box = (maxx - minx) * (maxy - miny);
    if (box <= CLIP_EPS_ABS) return 1;
    return area <= 1e-9 * box;
}

int clip_window_duplicate(const CPoly *a, const CPoly *b) {
    if (!a || !b || a->count != b->count) return 0;
    if (clip_window_empty(a) || clip_window_empty(b)) return 0;
    double aa = clip_area_kahan(a), ab = clip_area_kahan(b);
    if (aa < 0) aa = -aa;
    if (ab < 0) ab = -ab;
    CPoly inter;
    if (clip_convex(a->pts, a->count, b->pts, b->count, &inter) != 0) return 0;
    double ai = clip_area_kahan(&inter);
    if (ai < 0) ai = -ai;
    /* 交集面积与两者面积均几乎相等 => 完全重合（重复窗口） */
    return ai >= aa * (1.0 - 1e-9) && ai >= ab * (1.0 - 1e-9);
}

void clip_prefix_intersection(const CPoly *wins, const int *order, int k, CPoly *q) {
    *q = wins[order[0]];
    for (int i = 1; i < k; ++i) {
        CPoly nxt;
        clip_convex(q->pts, q->count,
                    wins[order[i]].pts, wins[order[i]].count, &nxt);
        *q = nxt;
        if (q->count < 3) break;
    }
}

static double now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

#ifndef CLIP_TIME_REPS
#define CLIP_TIME_REPS 7   /* 每排列重复计时次数，取中位数以抑制时钟抖动 */
#endif

static double median_ns(double *t, int n) {
    for (int i = 1; i < n; ++i) {
        double x = t[i];
        int j = i - 1;
        while (j >= 0 && t[j] > x) { t[j + 1] = t[j]; --j; }
        t[j + 1] = x;
    }
    return t[n / 2];
}

/* 评估一个排列；返回 0 合法，否则返回非法原因码，bad_a/bad_b 带出问题窗口 */
static int eval_perm(const CPoly *poly, const CPoly *wins, int m,
                     const int *order, double *cum, double *steps,
                     int *bad_a, int *bad_b) {
    *cum = 0.0;
    *bad_a = *bad_b = -1;
    for (int k = 0; k < m; ++k) steps[k] = 0.0;

    for (int k = 0; k < m; ++k) {
        if (clip_window_empty(&wins[order[k]])) {
            *bad_a = order[k];
            return CLIP_REASON_EMPTY;
        }
    }
    for (int i = 0; i < m; ++i)
        for (int j = i + 1; j < m; ++j)
            if (clip_window_duplicate(&wins[order[i]], &wins[order[j]])) {
                *bad_a = order[i];
                *bad_b = order[j];
                return CLIP_REASON_DUPWIN;
            }

    CPoly pk = *poly;                 /* 实际连续裁剪多边形 P_k */
    for (int k = 0; k < m; ++k) {
        CPoly nxt;
        if (pk.count < 3) {
            nxt.count = 0;
        } else {
            clip_convex(pk.pts, pk.count,
                        wins[order[k]].pts, wins[order[k]].count, &nxt);
        }
        pk = nxt;

        CPoly qk, sk;                /* 理想前缀交集 Q_k 与参照 S_k */
        clip_prefix_intersection(wins, order, k + 1, &qk);
        sk.count = 0;
        if (qk.count >= 3)
            clip_convex(poly->pts, poly->count, qk.pts, qk.count, &sk);

        double ap = clip_area_kahan(&pk);
        double as = clip_area_kahan(&sk);
        double e = ap - as;
        if (e < 0) e = -e;
        steps[k] = e;
        *cum += e;
        /* 不 break：即便中途裁空也要检查后续窗口是否为空/重复 */
    }
    return 0;
}

/* 字典序全排列递归枚举 */
static void enumerate(const CPoly *poly, const CPoly *wins, int m,
                      int *order, int *used, int depth, CVerdict *v) {
    if (depth == m) {
        int idx = v->nperm++;
        CPermResult *r = &v->res[idx];
        for (int i = 0; i < m; ++i) r->order[i] = order[i];

        double t0 = now_ns();
        int reason = eval_perm(poly, wins, m, order,
                               &r->cum_err, r->step_err, &r->bad_a, &r->bad_b);
        if (reason) {
            r->time_ns = now_ns() - t0;
            r->legal = 0;
            r->reason = reason;
            r->cum_err = 0.0;
            return;
        }
        /* 合法排列重复计时取中位数，抑制时钟抖动；误差以首次评估为准 */
        double rep[CLIP_TIME_REPS];
        rep[0] = now_ns() - t0;
        for (int ri = 1; ri < CLIP_TIME_REPS; ++ri) {
            double cum2; double steps2[CLIP_MAX_WINS];
            int ba2, bb2;
            double a = now_ns();
            (void)eval_perm(poly, wins, m, order, &cum2, steps2, &ba2, &bb2);
            rep[ri] = now_ns() - a;
        }
        r->time_ns = median_ns(rep, CLIP_TIME_REPS);
        r->legal = 1;
        r->reason = 0;
        if (v->best < 0 || r->cum_err < v->best_err) {
            v->best = idx;
            v->best_err = r->cum_err;
            for (int i = 0; i < m; ++i) v->best_order[i] = order[i];
        }
        return;
    }
    for (int w = 0; w < m; ++w) {
        if (used[w]) continue;
        used[w] = 1;
        order[depth] = w;
        enumerate(poly, wins, m, order, used, depth + 1, v);
        used[w] = 0;
    }
}

int clip_verdict(const CPoly *poly, const CPoly *wins, int m, CVerdict *v) {
    if (!poly || !wins || !v || m < 1) return -1;
    if (m > CLIP_MAX_WINS) return -2;
    if (poly->count < 3) return -1;

    v->nperm = 0;
    v->best = -1;
    v->best_err = 0.0;
    v->total_ns = 0.0;

    int order[CLIP_MAX_WINS], used[CLIP_MAX_WINS] = {0};
    double t0 = now_ns();
    enumerate(poly, wins, m, order, used, 0, v);
    v->total_ns = now_ns() - t0;

    if (v->best < 0) return -3;
    return 0;
}
#endif
