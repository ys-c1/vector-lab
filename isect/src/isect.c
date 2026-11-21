#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out) {
    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double den = cross2(rx, ry, sx, sy);
    if (fabs(den) < 1e-12) return 0;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    if (out) {
        out->t = t; out->u = u;
        out->at.x = a0.x + rx * t;
        out->at.y = a0.y + ry * t;
    }
    return 1;
}

int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits) {
    if (!a || !b || !hits || na < 2 || nb < 2) return -1;
    int k = 0;
    for (int i = 0; i + 1 < na; ++i)
        for (int j = 0; j + 1 < nb; ++j)
            if (isect_seg(a[i], a[i+1], b[j], b[j+1], 0)) ++k;
    *hits = k;
    return 0;
}

int isect_self(const IPoint *p, int n, int *first_a, int *first_b) {
    if (!p || !first_a || !first_b || n < 4) return -1;
    for (int i = 0; i + 1 < n; ++i) {
        for (int j = i + 2; j + 1 < n; ++j) {
            if (i == 0 && j == n - 2) continue;
            if (isect_seg(p[i], p[i+1], p[j], p[j+1], 0)) {
                *first_a = i; *first_b = j;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- cubic bezier curve vs polyline ---- */

static IPoint cubic_eval(const IPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    IPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

const char *isect_err_field(int code) {
    switch (code) {
        case ISECT_ERR_ARG: return "arg";
        case ISECT_ERR_SEGMENTS: return "segments";
        case ISECT_ERR_TOL: return "tol";
        default: return 0;
    }
}

/* signed cross of segment direction with (p - q0) */
static double line_side(IPoint q0, IPoint q1, IPoint p) {
    return (q1.x - q0.x) * (p.y - q0.y) - (q1.y - q0.y) * (p.x - q0.x);
}

static void trace_push(ITraceLog *log, int hit, int step,
                       double t0, double t1, double tm, IPoint at, double resid) {
    if (!log || !log->rows || log->count >= log->cap) return;
    ITrace *r = &log->rows[log->count++];
    r->hit = hit; r->step = step;
    r->t0 = t0; r->t1 = t1; r->tm = tm;
    r->at = at; r->resid = resid;
}

/* Bisect the curve bracket [seg_i/nseg, (seg_i+1)/nseg] against the
 * supporting line of segment q0-q1 until the curve point is within tol
 * of the line. Falls back to the coarse chord hit if the refined root
 * lands outside the segment. */
static void refine_hit(const IPoint c[4], IPoint q0, IPoint q1,
                       int seg_i, int seg_j, int nseg,
                       double tol, int max_iter,
                       int hit_index, ITraceLog *trace,
                       const IHit *coarse, IHit *out) {
    double dx = q1.x - q0.x, dy = q1.y - q0.y;
    double len = hypot(dx, dy);
    double t0 = seg_i / (double)nseg, t1 = (seg_i + 1) / (double)nseg;
    double f0 = line_side(q0, q1, cubic_eval(c, t0));
    double f1 = line_side(q0, q1, cubic_eval(c, t1));
    double tm;
    IPoint pm;

    if (f0 == 0.0) {
        tm = t0;
    } else if (f1 == 0.0) {
        tm = t1;
    } else {
        tm = 0.5 * (t0 + t1);
        for (int step = 1; step <= max_iter; ++step) {
            tm = 0.5 * (t0 + t1);
            pm = cubic_eval(c, tm);
            double fm = line_side(q0, q1, pm);
            double resid = fabs(fm) / len;
            trace_push(trace, hit_index, step, t0, t1, tm, pm, resid);
            if (resid < tol || 0.5 * (t1 - t0) < 1e-17) break;
            if (f0 * fm <= 0.0) { t1 = tm; f1 = fm; }
            else                { t0 = tm; f0 = fm; }
        }
    }
    pm = cubic_eval(c, tm);
    double u = ((pm.x - q0.x) * dx + (pm.y - q0.y) * dy) / (len * len);
    if (u < -1e-9 || u > 1.0 + 1e-9) {
        /* refined root missed the segment: keep the coarse chord hit */
        out->t = (seg_i + coarse->t) / nseg;
        out->u = seg_j + coarse->u;
        out->at = coarse->at;
        return;
    }
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    out->t = tm;
    out->u = seg_j + u;
    out->at = pm;
}

int isect_curve_polyline(const IPoint c[4], const IPoint *poly, int np,
                         const ICurveOpts *opts, IHit *out, int out_cap,
                         ITraceLog *trace) {
    if (!c || !poly || !opts || np < 2 || out_cap < 0 || (out_cap > 0 && !out))
        return ISECT_ERR_ARG;
    if (opts->segments <= 0) return ISECT_ERR_SEGMENTS;
    if (!(opts->tol > 0.0)) return ISECT_ERR_TOL;  /* also rejects NaN */
    int n = opts->segments;
    int max_iter = opts->max_iter > 0 ? opts->max_iter : 64;

    IPoint *pts = malloc((size_t)(n + 1) * sizeof *pts);
    if (!pts) return ISECT_ERR_ARG;
    for (int i = 0; i <= n; ++i) pts[i] = cubic_eval(c, i / (double)n);

    int nfound = 0;
    for (int i = 0; i < n && nfound < out_cap; ++i) {
        for (int j = 0; j + 1 < np && nfound < out_cap; ++j) {
            IHit h;
            if (!isect_seg(pts[i], pts[i + 1], poly[j], poly[j + 1], &h)) continue;
            IHit r;
            if (opts->refine)
                refine_hit(c, poly[j], poly[j + 1], i, j, n,
                           opts->tol, max_iter, nfound, trace, &h, &r);
            else {
                r.t = (i + h.t) / n;
                r.u = j + h.u;
                r.at = h.at;
            }
            out[nfound++] = r;
        }
    }
    free(pts);
    return nfound;
}

int isect_trace_write(const ITraceLog *log, const char *path) {
    if (!log || !path) return -1;
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "hit,step,t0,t1,tm,x,y,resid\n");
    for (int i = 0; i < log->count; ++i) {
        const ITrace *r = &log->rows[i];
        fprintf(fp, "%d,%d,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g\n",
                r->hit, r->step, r->t0, r->t1, r->tm, r->at.x, r->at.y, r->resid);
    }
    return fclose(fp) == 0 ? 0 : -1;
}
