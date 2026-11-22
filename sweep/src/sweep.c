#include "sweep.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define EPS 1e-12

/* ---------------------------------------------------------------- 基础 */

typedef struct { double t; int kind; int idx; } Ev; /* kind: 0 进入, 1 离开 */

static int cmp_ev(const void *pa, const void *pb) {
    const Ev *a = (const Ev *)pa, *b = (const Ev *)pb;
    if (a->t < b->t) return -1;
    if (a->t > b->t) return 1;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    return a->idx < b->idx ? -1 : (a->idx > b->idx);
}

static int cmp_double(const void *pa, const void *pb) {
    double a = *(const double *)pa, b = *(const double *)pb;
    if (a < b) return -1;
    if (a > b) return 1;
    return 0;
}

typedef struct {
    const SwSeg *segs;
    int n;
    double *lo, *hi;
    unsigned char *zero;   /* lo == hi：扫描方向零长段 */
    Ev *ev;
    int nev;
    double *times;         /* 不同事件时刻，升序 */
    int M;
    int *cnt;              /* 每个时刻的活动段数（含零长段） */
    int *zc;               /* 每个时刻锚定的零长段数 */
} Ctx;

static void ctx_free(Ctx *c) {
    free(c->lo); free(c->hi); free(c->zero);
    free(c->ev); free(c->times); free(c->cnt); free(c->zc);
    memset(c, 0, sizeof *c);
}

/* times 中等于 v 的下标；不存在返回 -1。 */
static int time_index(const Ctx *c, double v) {
    int lo = 0, hi = c->M - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if (c->times[mid] < v) lo = mid + 1;
        else if (c->times[mid] > v) hi = mid - 1;
        else return mid;
    }
    return -1;
}

static int ctx_build(const SwSeg *segs, int n, Ctx *c) {
    memset(c, 0, sizeof *c);
    if (!segs || n < 1) return SW_E_ARG;
    for (int i = 0; i < n; ++i) {
        if (!isfinite(segs[i].x0) || !isfinite(segs[i].y0) ||
            !isfinite(segs[i].x1) || !isfinite(segs[i].y1))
            return SW_E_SEG;
    }
    c->segs = segs; c->n = n;
    c->lo = malloc(sizeof(double) * (size_t)n);
    c->hi = malloc(sizeof(double) * (size_t)n);
    c->zero = malloc((size_t)n);
    c->ev = malloc(sizeof(Ev) * (size_t)(2 * n));
    c->times = malloc(sizeof(double) * (size_t)(2 * n));
    if (!c->lo || !c->hi || !c->zero || !c->ev || !c->times) { ctx_free(c); return SW_E_NOMEM; }

    for (int i = 0; i < n; ++i) {
        double a = segs[i].x0, b = segs[i].x1;
        c->lo[i] = a < b ? a : b;
        c->hi[i] = a < b ? b : a;
        c->zero[i] = (c->lo[i] == c->hi[i]);
        c->ev[2*i]   = (Ev){ c->lo[i], 0, i };
        c->ev[2*i+1] = (Ev){ c->hi[i], 1, i };
    }
    qsort(c->ev, (size_t)(2 * n), sizeof(Ev), cmp_ev);
    c->nev = 2 * n;

    /* 不同时刻 */
    c->M = 0;
    for (int k = 0; k < c->nev; ++k)
        if (c->M == 0 || c->ev[k].t != c->times[c->M - 1])
            c->times[c->M++] = c->ev[k].t;

    c->cnt = calloc((size_t)c->M, sizeof(int));
    c->zc = calloc((size_t)c->M, sizeof(int));
    int *diff = calloc((size_t)(c->M + 1), sizeof(int));
    if (!c->cnt || !c->zc || !diff) { free(diff); ctx_free(c); return SW_E_NOMEM; }
    for (int i = 0; i < n; ++i) {
        int k0 = time_index(c, c->lo[i]);
        int k1 = time_index(c, c->hi[i]);
        if (k0 < 0 || k1 < 0) { free(diff); ctx_free(c); return SW_E_ARG; }
        ++diff[k0];
        --diff[k1 + 1];
        if (c->zero[i]) ++c->zc[k0];
    }
    int run = 0;
    for (int k = 0; k < c->M; ++k) {
        run += diff[k];
        c->cnt[k] = run;
    }
    free(diff);
    return SW_OK;
}

/* 与 isect 模块 isect_seg 相同的窄相公式（逐位一致）。 */
static int seg_hit(const SwSeg *a, const SwSeg *b,
                   double *pt, double *pu, double *px, double *py) {
    double rx = a->x1 - a->x0, ry = a->y1 - a->y0;
    double sx = b->x1 - b->x0, sy = b->y1 - b->y0;
    double den = rx * sy - ry * sx;
    if (fabs(den) < EPS) return 0;
    double qx = b->x0 - a->x0, qy = b->y0 - a->y0;
    double t = (qx * sy - qy * sx) / den;
    double u = (qx * ry - qy * rx) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    *pt = t; *pu = u;
    *px = a->x0 + rx * t;
    *py = a->y0 + ry * t;
    return 1;
}

static long long c2(long long k) { return k >= 2 ? k * (k - 1) / 2 : 0; }

typedef struct { SwHit *v; int n, cap; } HitBuf;

static int hb_push(HitBuf *h, int snap, int ia, int ib,
                   double t, double u, double x, double y) {
    if (h->n == h->cap) {
        int nc = h->cap ? h->cap * 2 : 64;
        SwHit *nv = realloc(h->v, sizeof(SwHit) * (size_t)nc);
        if (!nv) return SW_E_NOMEM;
        h->v = nv; h->cap = nc;
    }
    SwHit *r = &h->v[h->n++];
    r->snap = snap; r->ia = ia; r->ib = ib;
    r->t = t; r->u = u; r->x = x; r->y = y;
    return SW_OK;
}

static void hb_free(HitBuf *h) { free(h->v); memset(h, 0, sizeof *h); }

typedef struct { int a, b; } PairKey;

static int cmp_pair(const void *pa, const void *pb) {
    const PairKey *x = (const PairKey *)pa, *y = (const PairKey *)pb;
    if (x->a != y->a) return x->a < y->a ? -1 : 1;
    return x->b < y->b ? -1 : (x->b > y->b);
}

/* 去重统计：unique_hits / hits_zero。 */
static void finalize_unique(HitBuf *h, const unsigned char *zero, SwStats *st) {
    int n = h->n;
    PairKey *keys = malloc(sizeof(PairKey) * (size_t)(n ? n : 1));
    int nu = 0, nz = 0;
    if (!keys) return;
    for (int i = 0; i < n; ++i) {
        keys[i].a = h->v[i].ia;
        keys[i].b = h->v[i].ib;
    }
    qsort(keys, (size_t)n, sizeof(PairKey), cmp_pair);
    for (int i = 0; i < n; ++i) {
        if (i > 0 && keys[i].a == keys[i-1].a && keys[i].b == keys[i-1].b) continue;
        ++nu;
        if (zero[keys[i].a] || zero[keys[i].b]) ++nz;
    }
    free(keys);
    st->unique_hits = nu;
    st->hits_zero = nz;
}

static void fill_common(const Ctx *c, SwStats *st) {
    st->events = c->nev;
    st->times = c->M;
    st->zero_gap_events = c->nev - c->M;
    st->zero_segs = 0;
    st->zero_buckets = 0;
    for (int i = 0; i < c->n; ++i) if (c->zero[i]) ++st->zero_segs;
    for (int k = 0; k < c->M; ++k) if (c->zc[k] > 0) ++st->zero_buckets;
    st->miss_ia = st->miss_ib = -1;
    st->miss_t0 = st->miss_t1 = st->miss_t = st->miss_x = st->miss_y = 0.0;
}

/* ---------------------------------------------------------------- 旧路径 */

int sweep_legacy(const SwSeg *segs, int n, SwHit *out, int cap,
                 int *nhits, SwStats *st) {
    Ctx c;
    int rc = ctx_build(segs, n, &c);
    if (rc != SW_OK) return rc;

    unsigned char *act = calloc((size_t)n, 1);
    int *lst = malloc(sizeof(int) * (size_t)n);
    HitBuf hb = {0};
    SwStats s;
    memset(&s, 0, sizeof s);
    fill_common(&c, &s);
    s.window = 0.0;
    s.merge = 0;
    s.audited = 0;
    s.groups = c.M;
    s.safe = SW_SAFE_UNKNOWN;
    s.reject_reason = SW_REJ_NONE;
    s.target_met = 0;

    long long cand = 0, zcand = 0;
    if (!act || !lst) { free(act); free(lst); ctx_free(&c); return SW_E_NOMEM; }

    for (int e = 0; e < c.nev; ++e) {
        const Ev *ev = &c.ev[e];
        if (ev->kind == 0) act[ev->idx] = 1;
        else               act[ev->idx] = 0;
        int k = 0;
        for (int i = 0; i < n; ++i) if (act[i]) lst[k++] = i;
        for (int a = 0; a < k; ++a) {
            for (int b = a + 1; b < k; ++b) {
                int ia = lst[a], ib = lst[b];
                ++cand;
                if (c.zero[ia] || c.zero[ib]) ++zcand;
                double t, u, x, y;
                if (seg_hit(&segs[ia], &segs[ib], &t, &u, &x, &y)) {
                    rc = hb_push(&hb, e, ia, ib, t, u, x, y);
                    if (rc != SW_OK) {
                        free(act); free(lst); hb_free(&hb); ctx_free(&c);
                        return rc;
                    }
                }
            }
        }
    }

    s.cand_legacy = cand;
    s.cand_merged = cand;
    s.cand_zero = zcand;
    s.cand_est = cand;      /* 逐事件路径的“估计”就是自身 */
    s.audit_pairs = 0;
    s.hits = hb.n;
    finalize_unique(&hb, c.zero, &s);

    if (out) {
        if (hb.n > cap) {
            free(act); free(lst); hb_free(&hb); ctx_free(&c);
            return SW_E_CAP;
        }
        memcpy(out, hb.v, sizeof(SwHit) * (size_t)hb.n);
    }
    if (nhits) *nhits = hb.n;
    if (st) *st = s;

    free(act); free(lst); hb_free(&hb); ctx_free(&c);
    return SW_OK;
}

/* ---------------------------------------------------------------- 合并路径 */

typedef struct { int a, b; } Group;

static int build_groups(const Ctx *c, double W, Group **out, int *ng) {
    int cap = c->M;
    Group *g = malloc(sizeof(Group) * (size_t)cap);
    if (!g) return SW_E_NOMEM;
    int n = 0, a = 0;
    while (a < c->M) {
        int b = a;
        while (b + 1 < c->M && c->times[b + 1] - c->times[b] <= W) ++b;
        g[n].a = a; g[n].b = b;
        ++n;
        a = b + 1;
    }
    *out = g; *ng = n;
    return SW_OK;
}

/* 组内代表时刻下标：活动段数最多，平局取最早。 */
static int group_rep(const Ctx *c, Group g) {
    int kr = g.a;
    for (int k = g.a + 1; k <= g.b; ++k)
        if (c->cnt[k] > c->cnt[kr]) kr = k;
    return kr;
}

static int run_merged(const SwSeg *segs, int n, double W, unsigned flags,
                      HitBuf *hb, SwStats *st) {
    Ctx c;
    int rc = ctx_build(segs, n, &c);
    if (rc != SW_OK) return rc;
    Group *gs = NULL;
    int ng = 0;
    rc = build_groups(&c, W, &gs, &ng);
    if (rc != SW_OK) { ctx_free(&c); return rc; }

    SwStats s;
    memset(&s, 0, sizeof s);
    fill_common(&c, &s);
    s.window = W;
    s.merge = 1;
    s.audited = (flags & SW_F_AUDIT) != 0;
    s.groups = ng;
    s.safe = s.audited ? SW_SAFE : SW_SAFE_UNKNOWN;
    s.reject_reason = SW_REJ_NONE;
    s.target_met = 0;

    long long cand = 0, zcand = 0, est = 0, audit = 0;
    int miss_found = 0;

    for (int gi = 0; gi < ng; ++gi) {
        Group g = gs[gi];
        int kr = group_rep(&c, g);
        double rep = c.times[kr];
        double t0 = c.times[g.a], t1 = c.times[g.b];

        /* 估计式：代表时刻的非零活动段两两配对 */
        est += c2((long long)(c.cnt[kr] - c.zc[kr]));

        /* 代表时刻快照：只取非零长段 */
        int k = 0;
        int *lst = malloc(sizeof(int) * (size_t)n);
        if (!lst) { free(gs); ctx_free(&c); return SW_E_NOMEM; }
        for (int i = 0; i < n; ++i)
            if (!c.zero[i] && c.lo[i] <= rep && c.hi[i] >= rep) lst[k++] = i;
        for (int a = 0; a < k; ++a) {
            for (int b = a + 1; b < k; ++b) {
                int ia = lst[a], ib = lst[b];
                ++cand;
                double t, u, x, y;
                if (seg_hit(&segs[ia], &segs[ib], &t, &u, &x, &y)) {
                    rc = hb_push(hb, gi, ia, ib, t, u, x, y);
                    if (rc != SW_OK) { free(lst); free(gs); ctx_free(&c); return rc; }
                }
            }
        }
        free(lst);

        /* 零长段桶：按各自唯一时刻单独成桶，只配“至少一端为零长段”的对 */
        for (int kk = g.a; kk <= g.b; ++kk) {
            if (c.zc[kk] == 0) continue;
            double tt = c.times[kk];
            est += c2((long long)c.cnt[kk]) - c2((long long)(c.cnt[kk] - c.zc[kk]));
            int m = 0;
            int *la = malloc(sizeof(int) * (size_t)n);
            if (!la) { free(gs); ctx_free(&c); return SW_E_NOMEM; }
            for (int i = 0; i < n; ++i)
                if (c.lo[i] <= tt && c.hi[i] >= tt) la[m++] = i;
            for (int a = 0; a < m; ++a) {
                for (int b = a + 1; b < m; ++b) {
                    int ia = la[a], ib = la[b];
                    int za = c.zero[ia] && c.lo[ia] == tt;
                    int zb = c.zero[ib] && c.lo[ib] == tt;
                    if (!za && !zb) continue;
                    ++cand; ++zcand;
                    double t, u, x, y;
                    if (seg_hit(&segs[ia], &segs[ib], &t, &u, &x, &y)) {
                        rc = hb_push(hb, gi, ia, ib, t, u, x, y);
                        if (rc != SW_OK) { free(la); free(gs); ctx_free(&c); return rc; }
                    }
                }
            }
            free(la);
        }

        /* 审计：窗口并集里未被覆盖的段对逐对做真实相交测试 */
        if (s.audited && !miss_found) {
            int m = 0;
            int *un = malloc(sizeof(int) * (size_t)n);
            if (!un) { free(gs); ctx_free(&c); return SW_E_NOMEM; }
            for (int i = 0; i < n; ++i)
                if (c.lo[i] <= t1 && c.hi[i] >= t0) un[m++] = i;
            for (int a = 0; a < m; ++a) {
                for (int b = a + 1; b < m; ++b) {
                    int ia = un[a], ib = un[b];
                    /* 被代表快照覆盖？ */
                    int cov = !c.zero[ia] && !c.zero[ib] &&
                              c.lo[ia] <= rep && c.hi[ia] >= rep &&
                              c.lo[ib] <= rep && c.hi[ib] >= rep;
                    /* 被零长段桶覆盖？ */
                    if (!cov) {
                        if (c.zero[ia] && c.lo[ia] >= t0 && c.lo[ia] <= t1 &&
                            c.lo[ib] <= c.lo[ia] && c.hi[ib] >= c.lo[ia]) cov = 1;
                        else if (c.zero[ib] && c.lo[ib] >= t0 && c.lo[ib] <= t1 &&
                                 c.lo[ia] <= c.lo[ib] && c.hi[ia] >= c.lo[ib]) cov = 1;
                    }
                    if (cov) continue;
                    ++audit;
                    double t, u, x, y;
                    if (seg_hit(&segs[ia], &segs[ib], &t, &u, &x, &y)) {
                        miss_found = 1;
                        s.safe = SW_UNSAFE;
                        s.reject_reason = SW_REJ_MISS;
                        s.miss_ia = ia; s.miss_ib = ib;
                        s.miss_t0 = t0; s.miss_t1 = t1; s.miss_t = rep;
                        s.miss_x = x; s.miss_y = y;
                    }
                }
            }
            free(un);
        }
    }

    s.cand_merged = cand;
    s.cand_zero = zcand;
    s.cand_est = est;
    s.audit_pairs = audit;
    s.hits = hb->n;
    finalize_unique(hb, c.zero, &s);
    if (st) *st = s;
    free(gs);
    ctx_free(&c);
    return SW_OK;
}

/* ---------------------------------------------------------------- 对外 */

int sweep_run(const SwSeg *segs, int n, double W, unsigned flags,
              SwHit *out, int cap, int *nhits, SwStats *st) {
    if (!segs || n < 1) return SW_E_ARG;
    if (flags & ~(SW_F_MERGE | SW_F_AUDIT)) return SW_E_FLAGS;
    if (!(flags & SW_F_MERGE)) return sweep_legacy(segs, n, out, cap, nhits, st);
    if (!isfinite(W) || W < 0.0) return SW_E_WINDOW;

    HitBuf hb = {0};
    SwStats s;
    int rc = run_merged(segs, n, W, flags, &hb, &s);
    if (rc != SW_OK) { hb_free(&hb); return rc; }
    if (out && hb.n > cap) { hb_free(&hb); return SW_E_CAP; }
    if (out) memcpy(out, hb.v, sizeof(SwHit) * (size_t)hb.n);
    if (nhits) *nhits = hb.n;
    if (st) *st = s;
    hb_free(&hb);
    return SW_OK;
}

int sweep_gap_dist(const SwSeg *segs, int n, SwGap *out, int cap,
                   int *nout, int *zero_gap_events, int *zero_span_segs) {
    Ctx c;
    int rc = ctx_build(segs, n, &c);
    if (rc != SW_OK) return rc;
    if (zero_gap_events) *zero_gap_events = c.nev - c.M;
    if (zero_span_segs) {
        int z = 0;
        for (int i = 0; i < n; ++i) if (c.zero[i]) ++z;
        *zero_span_segs = z;
    }

    double *gaps = malloc(sizeof(double) * (size_t)(c.M > 1 ? c.M - 1 : 1));
    if (!gaps) { ctx_free(&c); return SW_E_NOMEM; }
    int ng = 0;
    for (int k = 0; k + 1 < c.M; ++k) {
        double d = c.times[k+1] - c.times[k];
        if (d > 0.0) gaps[ng++] = d;
    }
    qsort(gaps, (size_t)ng, sizeof(double), cmp_double);

    int nw = 0, overflow = 0;
    for (int i = 0; i < ng;) {
        int j = i + 1;
        while (j < ng && gaps[j] == gaps[i]) ++j;
        if (out && nw < cap) { out[nw].gap = gaps[i]; out[nw].count = j - i; }
        else if (out) overflow = 1;
        ++nw;
        i = j;
    }
    if (nout) *nout = nw;
    free(gaps);
    ctx_free(&c);
    return overflow ? SW_E_CAP : SW_OK;
}

int sweep_estimate_pairs(const SwSeg *segs, int n, double W, long long *est) {
    if (!segs || n < 1) return SW_E_ARG;
    if (!isfinite(W) || W < 0.0) return SW_E_WINDOW;
    Ctx c;
    int rc = ctx_build(segs, n, &c);
    if (rc != SW_OK) return rc;
    Group *gs = NULL;
    int ng = 0;
    rc = build_groups(&c, W, &gs, &ng);
    if (rc != SW_OK) { ctx_free(&c); return rc; }
    long long e = 0;
    for (int gi = 0; gi < ng; ++gi) {
        Group g = gs[gi];
        int kr = group_rep(&c, g);
        e += c2((long long)(c.cnt[kr] - c.zc[kr]));
        for (int kk = g.a; kk <= g.b; ++kk) {
            if (c.zc[kk] == 0) continue;
            e += c2((long long)c.cnt[kk]) - c2((long long)(c.cnt[kk] - c.zc[kk]));
        }
    }
    free(gs);
    ctx_free(&c);
    if (est) *est = e;
    return SW_OK;
}

int sweep_choose(const SwSeg *segs, int n, double target, unsigned flags,
                 double *bestW, SwStats *best) {
    if (!segs || n < 1) return SW_E_ARG;
    if (!isfinite(target) || target < 0.0 || target > 1.0) return SW_E_WINDOW;
    if (flags & ~(SW_F_MERGE | SW_F_AUDIT)) return SW_E_FLAGS;

    SwStats legacy;
    int rc = sweep_legacy(segs, n, NULL, 0, NULL, &legacy);
    if (rc != SW_OK) return rc;
    long long base = legacy.cand_legacy;

    Ctx c;
    rc = ctx_build(segs, n, &c);
    if (rc != SW_OK) return rc;

    /* 候选窗口：0 与所有正事件时间差（升序去重） */
    int cap = c.M;
    double *ws = malloc(sizeof(double) * (size_t)(cap ? cap : 1));
    if (!ws) { ctx_free(&c); return SW_E_NOMEM; }
    int nw = 0;
    ws[nw++] = 0.0;
    for (int k = 0; k + 1 < c.M; ++k) {
        double d = c.times[k+1] - c.times[k];
        if (d > 0.0) ws[nw++] = d;
    }
    qsort(ws, (size_t)nw, sizeof(double), cmp_double);
    int u = 0;
    for (int i = 0; i < nw; ++i)
        if (u == 0 || ws[i] != ws[u-1]) ws[u++] = ws[i];
    ctx_free(&c);

    int have = 0, have_target = 0;
    long long best_total = 0, best_cand = 0;
    SwStats bs;
    double bw = 0.0;
    for (int i = 0; i < u; ++i) {
        HitBuf hb = {0};
        SwStats s;
        rc = run_merged(segs, n, ws[i], flags | SW_F_AUDIT, &hb, &s);
        hb_free(&hb);
        if (rc != SW_OK) { free(ws); return rc; }
        if (s.safe != SW_SAFE) continue;   /* 只接受审计安全的窗口 */
        long long total = s.cand_merged + s.audit_pairs;
        int meets = s.cand_est <= (long long)((1.0 - target) * (double)base);
        if (meets) s.target_met = 1;
        if (!have ||
            (meets && !have_target) ||
            (meets == have_target &&
             (total < best_total || (total == best_total && ws[i] < bw)))) {
            have = 1; have_target = meets;
            best_total = total; best_cand = s.cand_merged;
            bw = ws[i]; bs = s;
        }
    }
    free(ws);
    if (!have) return SW_E_WINDOW;
    (void)best_cand;
    bs.target_met = have_target;
    if (bestW) *bestW = bw;
    if (best) *best = bs;
    return SW_OK;
}

int sweep_hits_csv(const SwHit *hits, int nhits, FILE *fp) {
    if (!fp || (nhits > 0 && !hits)) return SW_E_ARG;
    for (int i = 0; i < nhits; ++i) {
        const SwHit *h = &hits[i];
        if (fprintf(fp, "%d,%d,%d,%.17g,%.17g,%.17g,%.17g\n",
                    h->snap, h->ia, h->ib, h->t, h->u, h->x, h->y) < 0)
            return SW_E_IO;
    }
    return nhits;
}

int sweep_format_reject(const SwStats *st, char *buf, int cap) {
    if (!st || !buf || cap <= 0) return -1;
    int n;
    if (st->safe == SW_UNSAFE) {
        n = snprintf(buf, (size_t)cap,
            "拒绝：窗口 W=%g 的组 [%.17g, %.17g]（代表时刻 %.17g）漏掉相交段对 "
            "(%d,%d)，交点 (%.17g, %.17g)；该窗口过大，段对只在窗口内部短暂共存。",
            st->window, st->miss_t0, st->miss_t1, st->miss_t,
            st->miss_ia, st->miss_ib, st->miss_x, st->miss_y);
    } else if (st->safe == SW_SAFE) {
        n = snprintf(buf, (size_t)cap,
            "接受：窗口 W=%g，%d 组，审计 %lld 对风险段对，未发现漏交；"
            "候选对 %lld（零长段 %lld）。",
            st->window, st->groups, st->audit_pairs,
            st->cand_merged, st->cand_zero);
    } else {
        n = snprintf(buf, (size_t)cap,
            "未审计：窗口 W=%g，%d 组，候选对 %lld（零长段 %lld）；"
            "安全与否需要 SW_F_AUDIT。",
            st->window, st->groups, st->cand_merged, st->cand_zero);
    }
    return n;
}

const char *sweep_strerror(int code) {
    switch (code) {
    case SW_OK: return "ok";
    case SW_E_ARG: return "bad argument";
    case SW_E_SEG: return "non-finite segment endpoint";
    case SW_E_WINDOW: return "bad window";
    case SW_E_FLAGS: return "bad flags";
    case SW_E_CAP: return "capacity exceeded";
    case SW_E_IO: return "io failure";
    case SW_E_NOMEM: return "out of memory";
    default: return "unknown";
    }
}
