#define _POSIX_C_SOURCE 200809L
#include "alloc.h"
#include "raster.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static long cost_of(int w, int h, int ss) {
    return (long)w * (long)h * (long)ss * (long)ss;
}

/* ---------- 精确覆盖率：多边形 × 轴对齐矩形 ---------- */

/* 简单多边形与矩形 [x0,x1]×[y0,y1] 的精确相交面积。
 * Sutherland–Hodgman 四刀；tmp 至少 4*(2*n+8) 个 double。 */
static double poly_rect_area(const RPoint *pts, int n, double *tmp,
                             double x0, double y0, double x1, double y1)
{
    int cap = 2 * n + 8;
    double *ax = tmp,           *ay = tmp + cap;
    double *bx = tmp + 2 * cap, *by = tmp + 3 * cap;
    for (int i = 0; i < n; ++i) { ax[i] = pts[i].x; ay[i] = pts[i].y; }
    int m = n;
    const double px[4] = { 1, 0, -1, 0 };
    const double py[4] = { 0, 1, 0, -1 };
    const double pc[4] = { x0, y0, -x1, -y1 };
    for (int e = 0; e < 4; ++e) {
        int k = 0;
        for (int i = 0; i < m; ++i) {
            int j = (i + 1) % m;
            double va = px[e] * ax[i] + py[e] * ay[i] - pc[e];
            double vb = px[e] * ax[j] + py[e] * ay[j] - pc[e];
            int ina = va >= 0, inb = vb >= 0;
            if (ina) { bx[k] = ax[i]; by[k] = ay[i]; ++k; }
            if (ina != inb) {
                double t = va / (va - vb);
                bx[k] = ax[i] + (ax[j] - ax[i]) * t;
                by[k] = ay[i] + (ay[j] - ay[i]) * t;
                ++k;
            }
        }
        m = k;
        double *sx = ax; ax = bx; bx = sx;
        double *sy = ay; ay = by; by = sy;
        if (m < 3) return 0.0;
    }
    double s = 0.0;
    for (int i = 0; i < m; ++i) {
        int j = (i + 1) % m;
        s += ax[i] * ay[j] - ax[j] * ay[i];
    }
    return fabs(s) * 0.5;
}

/* 场景并集在矩形内的覆盖率 ∈ [0,1]。
 * 逐多边形精确取面积后相加再截断：不重叠场景精确，部分重叠为保守高估。 */
static double scene_rect_coverage(const RScene *s, double *tmp,
                                  double x0, double y0, double x1, double y1)
{
    double area = 0.0;
    for (int k = 0; k < s->npolys; ++k) {
        const RPoly *p = &s->polys[k];
        area += poly_rect_area(p->pts, p->n, tmp, x0, y0, x1, y1);
    }
    double cell = (x1 - x0) * (y1 - y0);
    if (cell <= 0.0) return 0.0;
    double c = area / cell;
    return c > 1.0 ? 1.0 : c;
}


/* 把 w×h 覆盖率图面积加权缩放到 rw×rh（先纵向后横向）。 */
static void resample(const unsigned char *src, int w, int h,
                     int rw, int rh, double *out)
{
    double *tmp = (double *)malloc(sizeof(double) * (size_t)rh * (size_t)w);
    if (!tmp) return;
    const double inv255 = 1.0 / 255.0;
    for (int Y = 0; Y < rh; ++Y) {
        double a = (double)Y * h / rh, b = (double)(Y + 1) * h / rh;
        int j0 = (int)floor(a), j1 = (int)ceil(b);
        for (int x = 0; x < w; ++x) {
            double acc = 0.0;
            for (int j = j0; j < j1; ++j) {
                if (j < 0 || j >= h) continue;
                double l = a > j ? a : j, r = b < j + 1 ? b : j + 1;
                if (r > l) acc += (r - l) * src[(size_t)j * w + x] * inv255;
            }
            tmp[(size_t)Y * w + x] = acc / (b - a);
        }
    }
    for (int Y = 0; Y < rh; ++Y) {
        for (int X = 0; X < rw; ++X) {
            double a = (double)X * w / rw, b = (double)(X + 1) * w / rw;
            int i0 = (int)floor(a), i1 = (int)ceil(b);
            double acc = 0.0;
            for (int i = i0; i < i1; ++i) {
                if (i < 0 || i >= w) continue;
                double l = a > i ? a : i, r = b < i + 1 ? b : i + 1;
                if (r > l) acc += (r - l) * tmp[(size_t)Y * w + i];
            }
            out[(size_t)Y * rw + X] = acc / (b - a);
        }
    }
    free(tmp);
}

/* 亚像素相位：候选像素边界与显示格子边界可能恰好重合（w==rw 时），
 * 这种对齐会让某个尺寸白得一份误差红利。取两个相位（原相位 + 半格）
 * 求平均，消掉对齐红利。
 * 参考格子和候选窗口同时平移同一个偏移量，两者始终对齐；
 * 平移量以显示格子尺寸为单位（亚像素级），对所有候选一致。 */
static const double kPhaseX[RALLOC_PHASES] = { 0.0, 0.5 };
static const double kPhaseY[RALLOC_PHASES] = { 0.0, 0.5 };

static int build_reference(RAllocPlan *p, int ref_side) {
    double aw = p->win.maxx - p->win.minx;
    double ah = p->win.maxy - p->win.miny;
    int rw, rh;
    if (aw >= ah) { rw = ref_side; rh = (int)llround(ref_side * ah / aw); }
    else          { rh = ref_side; rw = (int)llround(ref_side * aw / ah); }
    if (rw < 2) rw = 2;
    if (rh < 2) rh = 2;
    p->ref_w = rw; p->ref_h = rh;
    size_t cells = (size_t)rw * (size_t)rh;
    p->ref = (double *)malloc(sizeof(double) * cells * RALLOC_PHASES);
    if (!p->ref) return -1;
    int maxn = 3;
    for (int k = 0; k < p->scene.npolys; ++k)
        if (p->scene.polys[k].n > maxn) maxn = p->scene.polys[k].n;
    int cap = 2 * maxn + 8;
    double *tmp = (double *)malloc(sizeof(double) * (size_t)(4 * cap));
    if (!tmp) { free(p->ref); p->ref = NULL; return -1; }
    double cw = aw / rw, ch = ah / rh;
    for (int q = 0; q < RALLOC_PHASES; ++q) {
        double *dst = p->ref + (size_t)q * cells;
        double ox = kPhaseX[q] * cw, oy = kPhaseY[q] * ch;
        p->phase_ox[q] = ox;
        p->phase_oy[q] = oy;
        for (int y = 0; y < rh; ++y) {
            double y0 = p->win.miny + oy + y * ch;
            for (int x = 0; x < rw; ++x) {
                double x0 = p->win.minx + ox + x * cw;
                dst[(size_t)y * rw + x] =
                    scene_rect_coverage(&p->scene, tmp, x0, y0, x0 + cw, y0 + ch);
            }
        }
    }
    free(tmp);
    return 0;
}

double ralloc_eval(RAllocPlan *p, int w, int h, int ss) {
    if (!p || !p->ref || w <= 0 || h <= 0) return -1.0;
    if (ss < 1) ss = 1;
    if (ss > RALLOC_MAX_SS) ss = RALLOC_MAX_SS;
    unsigned char *buf = (unsigned char *)malloc((size_t)w * (size_t)h);
    double *rs = (double *)malloc(sizeof(double) * (size_t)p->ref_w * (size_t)p->ref_h);
    if (!buf || !rs) { free(buf); free(rs); return -1.0; }
    size_t cells = (size_t)p->ref_w * (size_t)p->ref_h;
    double e = 0.0, stair = 0.0;
    for (int q = 0; q < RALLOC_PHASES; ++q) {
        /* 参考格子和候选窗口一起平移，保持对齐 */
        RBox win = p->win;
        win.minx += p->phase_ox[q]; win.maxx += p->phase_ox[q];
        win.miny += p->phase_oy[q]; win.maxy += p->phase_oy[q];
        if (raster_coverage(&p->scene, &win, w, h, ss, buf) != 0) {
            free(buf); free(rs);
            return -1.0;
        }
        resample(buf, w, h, p->ref_w, p->ref_h, rs);
        const double *ref = p->ref + (size_t)q * cells;
        for (size_t i = 0; i < cells; ++i) e += fabs(rs[i] - ref[i]);
        int rw = p->ref_w, rh = p->ref_h;
        for (int y = 0; y + 1 < rh; ++y)
            for (int x = 0; x + 1 < rw; ++x) {
                double d = rs[(size_t)y * rw + x]
                         + rs[(size_t)(y + 1) * rw + x + 1]
                         - rs[(size_t)y * rw + x + 1]
                         - rs[(size_t)(y + 1) * rw + x];
                stair += fabs(d);
            }
    }
    free(buf); free(rs);
    return e / ((double)cells * RALLOC_PHASES)
         + RALLOC_STAIR_WEIGHT * stair / ((double)cells * RALLOC_PHASES);
}

/* ---------- 候选表 ---------- */

/* 正方形像素、按窗口长宽比出图：长边 = L。 */
static void dims_for(double aspect, int L, int *w, int *h) {
    int ww, hh;
    if (aspect >= 1.0) { ww = L; hh = (int)llround((double)L / aspect); }
    else               { hh = L; ww = (int)llround((double)L * aspect); }
    if (ww < 1) ww = 1;
    if (hh < 1) hh = 1;
    *w = ww; *h = hh;
}

/* 尺寸阶梯公比 1.2 × ss=1..8。
 * 候选集定义为 { cost <= max_budget }，与 max_budget 一一对应，因此预算
 * 变大时候选集严格包含旧的，最优 J 单调不升是结构保证。 */
static int build_candidates(RAllocPlan *p, long max_budget) {
    double aw = p->win.maxx - p->win.minx, ah = p->win.maxy - p->win.miny;
    double aspect = aw / ah;
    int n = 0;
    for (double Lf = (double)RALLOC_MIN_SIDE; ; Lf *= 1.2) {
        int w, h;
        dims_for(aspect, (int)llround(Lf), &w, &h);
        if (w < RALLOC_MIN_SIDE || h < RALLOC_MIN_SIDE) continue;
        /* 该尺寸在 ss=1 时的代价已超预算，更大的尺寸只会更贵 */
        if (cost_of(w, h, 1) > max_budget) break;
        if (n > 0 && p->cand[n - 1].w == w && p->cand[n - 1].h == h) continue;
        for (int ss = 1; ss <= RALLOC_MAX_SS; ++ss) {
            long c = cost_of(w, h, ss);
            if (c > max_budget) break;
            if (n >= RALLOC_MAX_CAND) return n;
            p->cand[n].w = w; p->cand[n].h = h; p->cand[n].ss = ss;
            p->cand[n].cost = c; p->cand[n].jag = -1.0;
            ++n;
        }
        if (n >= RALLOC_MAX_CAND) break;
    }
    return n;
}

/* ---------- 对外接口 ---------- */

int ralloc_plan(const RScene *s, const RBox *win, int ref_side, long max_budget,
                RAllocPlan *p, RAllocReport *rep)
{
    if (!s || !p || s->npolys <= 0) return RALLOC_BAD_INPUT;
    if (ref_side <= 0) ref_side = RALLOC_REF_SIDE_DEFAULT;
    if (max_budget <= 0) max_budget = 1;
    memset(p, 0, sizeof(*p));
    p->scene = *s;
    p->ref = NULL;
    if (win) p->win = *win;
    else if (scene_bbox(s, &p->win) != 0) return RALLOC_BAD_INPUT;
    if (!(p->win.maxx > p->win.minx) || !(p->win.maxy > p->win.miny))
        return RALLOC_BAD_INPUT;

    double t0 = now_ms();
    if (build_reference(p, ref_side) != 0) return RALLOC_BAD_INPUT;
    p->n = build_candidates(p, max_budget);
    p->evaluated = 0;
    double t1 = now_ms();
    if (rep) {
        memset(rep, 0, sizeof(*rep));
        rep->budget = max_budget;
        rep->candidates = p->n;
        rep->ref_w = p->ref_w;
        rep->ref_h = p->ref_h;
        rep->sizes = 0;
        for (int i = 0; i < p->n; ++i)
            if (i == 0 || p->cand[i].w != p->cand[i - 1].w || p->cand[i].h != p->cand[i - 1].h)
                ++rep->sizes;
        rep->plan_ms = t1 - t0;
    }
    return RALLOC_OK;
}

void ralloc_plan_free(RAllocPlan *p) { if (p) { free(p->ref); p->ref = NULL; } }

int ralloc_pick(RAllocPlan *p, long budget, RAlloc *out) {
    if (!p || !out) return RALLOC_BAD_INPUT;
    memset(out, 0, sizeof(*out));
    if (budget <= 0) return RALLOC_BUDGET_ZERO;
    int best = -1;
    double bj = 0.0;
    double t0 = now_ms();
    for (int i = 0; i < p->n; ++i) {
        RAlloc *c = &p->cand[i];
        if (c->cost > budget) continue;
        if (c->jag < 0.0) {
            c->jag = ralloc_eval(p, c->w, c->h, c->ss);
            if (c->jag >= 0.0) ++p->evaluated;
        }
        if (c->jag < 0.0) continue;
        if (best < 0 || c->jag < bj - 1e-15) { best = i; bj = c->jag; }
    }
    p->eval_ms += now_ms() - t0;
    if (best < 0) return RALLOC_NO_FIT;
    *out = p->cand[best];
    return RALLOC_OK;
}

int ralloc_solve(const RScene *s, const RBox *win, long budget, int ref_side,
                 RAlloc *out, RAllocReport *rep)
{
    RAllocPlan p;
    RAllocReport local;
    int rc = ralloc_plan(s, win, ref_side, budget > 0 ? budget : 1, &p, &local);
    if (rc != RALLOC_OK) return rc;
    rc = ralloc_pick(&p, budget, out);
    if (rep) {
        local.evaluated = p.evaluated;
        local.eval_ms = p.eval_ms;
        *rep = local;
    }
    ralloc_plan_free(&p);
    return rc;
}

int ralloc_check(int w, int h, int ss, long budget, char *msg, int msglen)
{
    if (w <= 0 || h <= 0) {
        if (msg) snprintf(msg, msglen, "invalid size %dx%d", w, h);
        return RALLOC_BAD_INPUT;
    }
    int ss_bad = (ss < 1 || ss > RALLOC_MAX_SS);
    long size_cost = (long)w * (long)h;
    long full = size_cost * (long)ss * (long)ss;
    if (ss_bad && size_cost > budget) {
        if (msg) snprintf(msg, msglen,
            "size %dx%d needs %ld > budget %ld (size over by %ld); ss=%d outside 1..%d",
            w, h, size_cost, budget, size_cost - budget, ss, RALLOC_MAX_SS);
        return RALLOC_OVER_BOTH;
    }
    if (ss_bad) {
        if (msg) snprintf(msg, msglen, "ss=%d outside 1..%d", ss, RALLOC_MAX_SS);
        return RALLOC_OVER_RANGE;
    }
    if (size_cost > budget) {
        if (msg) snprintf(msg, msglen,
            "size %dx%d needs %ld > budget %ld (size over by %ld)",
            w, h, size_cost, budget, size_cost - budget);
        return RALLOC_OVER_SIZE;
    }
    if (full > budget) {
        if (msg) snprintf(msg, msglen,
            "size %dx%d ok (%ld), ss=%d makes it %ld > budget %ld (ss over by %ld)",
            w, h, size_cost, ss, full, budget, full - budget);
        return RALLOC_OVER_SS;
    }
    if (msg) snprintf(msg, msglen, "ok: %ld <= budget %ld", full, budget);
    return 0;
}
