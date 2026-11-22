#define _POSIX_C_SOURCE 199309L
#include "adapt.h"
#include <string.h>

static int inside(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static int scene_hit(const RScene *s, double x, double y) {
    for (int k = 0; k < s->npolys; ++k)
        if (inside(&s->polys[k], x, y)) return 1;
    return 0;
}

int adapt_ref_build(AdaptRef *ref) {
    if (!ref || !ref->scene || !ref->box || !ref->cov) return ADAPT_ERR_ARG;
    if (ref->w <= 0 || ref->h <= 0) return ADAPT_ERR_ARG;
    if (!(ref->box->maxx > ref->box->minx) ||
        !(ref->box->maxy > ref->box->miny))
        return ADAPT_ERR_ARG;
    const int Q = ADAPT_SS_REF;
    const RScene *s = ref->scene;
    double dx = (ref->box->maxx - ref->box->minx) / ref->w;
    double dy = (ref->box->maxy - ref->box->miny) / ref->h;
    size_t n = (size_t)ref->w * (size_t)ref->h;
    memset(ref->cov, 0, n * sizeof(int));
    for (int y = 0; y < ref->h; ++y)
        for (int x = 0; x < ref->w; ++x) {
            int hit = 0;
            for (int j = 0; j < Q; ++j)
                for (int i = 0; i < Q; ++i) {
                    double px = ref->box->minx + (x + (i + 0.5) / Q) * dx;
                    double py = ref->box->miny + (y + (j + 0.5) / Q) * dy;
                    if (scene_hit(s, px, py)) ++hit;
                }
            ref->cov[y * ref->w + x] = hit;
        }
    return ADAPT_OK;
}

int adapt_edge_metric(const AdaptRef *ref, int ss,
                      AdaptProbeFn probe, void *ctx, unsigned char *buf,
                      double *metric, int *band, int *bad) {
    if (!ref || !probe || !buf || !metric) return ADAPT_ERR_ARG;
    if (ss < ADAPT_SS_MIN || ss > ADAPT_SS_MAX) return ADAPT_ERR_ARG;
    int rc = probe(ss, ctx, buf);
    if (rc != 0) return rc < 0 ? rc : ADAPT_ERR_ARG;
    const double q2 = (double)ADAPT_SS_REF * ADAPT_SS_REF;
    int edge = 0, mismatch = 0;
    size_t n = (size_t)ref->w * (size_t)ref->h;
    for (size_t i = 0; i < n; ++i) {
        int c = ref->cov[i];
        if (c == 0 || c == (int)q2) continue;       /* 非边缘带：不参与统计 */
        ++edge;
        int ref_on = (c > 0);                    /* 与光栅器一致的 any-hit 规则 */
        int got_on = buf[i] != 0;
        if (ref_on != got_on) ++mismatch;
    }
    *metric = edge ? (double)mismatch / (double)edge : 0.0;
    if (band) *band = edge;
    if (bad) *bad = mismatch;
    return ADAPT_OK;
}

static double now_ms(void);

/* 单调性守卫：新点 (ss,m) 与每个已探测点双向配对，任一“大 ss 指标更高”即违例。 */
static int mono_violation(const int *seen, const double *metric_at,
                          int ss, double m, int *a, int *b,
                          double *ma, double *mb) {
    for (int s = ADAPT_SS_MIN; s <= ADAPT_SS_MAX; ++s) {
        if (!seen[s]) continue;
        if (s < ss && m > metric_at[s] + ADAPT_EPS) {
            *a = s; *b = ss; *ma = metric_at[s]; *mb = m; return 1;
        }
        if (s > ss && metric_at[s] > m + ADAPT_EPS) {
            *a = ss; *b = s; *ma = m; *mb = metric_at[s]; return 1;
        }
    }
    return 0;
}

static void fill_nonmono(AdaptResult *r, int band, int bad,
                         int a, int b, double ma, double mb) {
    r->ss = b;
    r->metric = mb;
    r->edge_pixels = band;
    r->bad_pixels = bad;
    r->mono_a = a;
    r->mono_b = b;
    r->metric_a = ma;
    r->metric_b = mb;
}

int adapt_solve(const AdaptTarget *t, AdaptResult *r) {
    if (!t || !r || !t->probe || !t->ref || !t->buf) return ADAPT_ERR_ARG;
    if (!(t->tol > 0.0) || t->tol > 1.0) return ADAPT_ERR_ARG;
    if (t->ss_lo < ADAPT_SS_MIN || t->ss_hi > ADAPT_SS_MAX ||
        t->ss_lo > t->ss_hi)
        return ADAPT_ERR_ARG;
    memset(r, 0, sizeof(*r));
    for (int ss = 0; ss <= ADAPT_SS_MAX; ++ss) {
        r->probe_ms[ss] = -1.0;
        r->metric_at[ss] = -1.0;
    }

    int lo = t->ss_lo, hi = t->ss_hi;
    int seen[ADAPT_SS_MAX + 1];
    memset(seen, 0, sizeof(seen));
    int code = ADAPT_OK;

    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        double t0 = now_ms();
        int band = 0, bad = 0;
        double m;
        int rc = adapt_edge_metric(t->ref, mid, t->probe, t->ctx,
                                   t->buf, &m, &band, &bad);
        double dt = now_ms() - t0;
        if (rc != ADAPT_OK) return rc;
        r->probe_ms[mid] = dt;
        r->metric_at[mid] = m;
        seen[mid] = 1;
        ++r->nprobes;

        /* 单调性守卫：指标不得随 ss 上升（与全部已探测点配对）。 */
        int va, vb; double vma, vmb;
        if (mono_violation(seen, r->metric_at, mid, m, &va, &vb, &vma, &vmb)) {
            fill_nonmono(r, band, bad, va, vb, vma, vmb);
            return ADAPT_ERR_NONMONO;
        }

        if (m <= t->tol + ADAPT_EPS) hi = mid;      /* 达标，尝试更小 */
        else lo = mid + 1;                          /* 不达标，抬高下界 */
    }

    /* lo == hi：验证最终候选（可能已在二分中探测过）。 */
    double t0 = now_ms();
    int band = 0, bad = 0;
    double m;
    int rc = adapt_edge_metric(t->ref, lo, t->probe, t->ctx,
                               t->buf, &m, &band, &bad);
    double dt = now_ms() - t0;
    if (rc != ADAPT_OK) return rc;
    if (!seen[lo]) ++r->nprobes;
    r->probe_ms[lo] = dt;
    r->metric_at[lo] = m;
    seen[lo] = 1;

    int va, vb; double vma, vmb;
    if (mono_violation(seen, r->metric_at, lo, m, &va, &vb, &vma, &vmb)) {
        fill_nonmono(r, band, bad, va, vb, vma, vmb);
        return ADAPT_ERR_NONMONO;
    }

    r->ss = lo;
    r->metric = m;
    r->edge_pixels = band;
    r->bad_pixels = bad;
    if (m > t->tol + ADAPT_EPS) {
        code = ADAPT_ERR_NOPASS;                   /* 区间上界仍不达标 */
    }
    return code;
}

const char *adapt_strerror(int code) {
    switch (code) {
    case ADAPT_OK: return "ok";
    case ADAPT_ERR_ARG: return "bad argument (ss must be in [1,8])";
    case ADAPT_ERR_NOPASS: return "metric still above tolerance at ss=8";
    case ADAPT_ERR_NONMONO: return "metric increased with ss; rejected";
    default: return "unknown error";
    }
}

#if defined(__linux__)
#include <time.h>
static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#else
#include <time.h>
static double now_ms(void) { return (double)clock() * 1000.0 / CLOCKS_PER_SEC; }
#endif
