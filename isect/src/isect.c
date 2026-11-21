#include "isect.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

static void set_err(char *err, int errcap, const char *fmt, ...) {
    if (!err || errcap <= 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, (size_t)errcap, fmt, ap);
    va_end(ap);
}

static int cmp_hit(const void *pa, const void *pb) {
    const IXHit *a = (const IXHit *)pa, *b = (const IXHit *)pb;
    if (a->at.x != b->at.x) return a->at.x < b->at.x ? -1 : 1;
    if (a->at.y != b->at.y) return a->at.y < b->at.y ? -1 : 1;
    if (a->ia != b->ia) return a->ia < b->ia ? -1 : 1;
    if (a->ib != b->ib) return a->ib < b->ib ? -1 : 1;
    return 0;
}

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

int isect_polyline_list(const IPoint *a, int na, const IPoint *b, int nb,
                        IXHit *out, int cap, int *nhits, int dedup_sort,
                        char *err, int errcap) {
    if (!a) { set_err(err, errcap, "arg a is NULL (polyline A, position 0)"); return ISECT_ERR_ARG; }
    if (!b) { set_err(err, errcap, "arg b is NULL (polyline B, position 0)"); return ISECT_ERR_ARG; }
    if (!out) { set_err(err, errcap, "arg out is NULL (hit buffer)"); return ISECT_ERR_ARG; }
    if (!nhits) { set_err(err, errcap, "arg nhits is NULL (hit count out-param)"); return ISECT_ERR_ARG; }
    if (na < 2) { set_err(err, errcap, "arg na=%d out of range (need >= 2, polyline A has %d point%s)",
                          na, na, na == 1 ? "" : "s"); return ISECT_ERR_ARG; }
    if (nb < 2) { set_err(err, errcap, "arg nb=%d out of range (need >= 2, polyline B has %d point%s)",
                          nb, nb, nb == 1 ? "" : "s"); return ISECT_ERR_ARG; }
    if (cap < 1) { set_err(err, errcap, "arg cap=%d out of range (need >= 1)", cap); return ISECT_ERR_ARG; }

    int k = 0;
    for (int i = 0; i + 1 < na; ++i) {
        for (int j = 0; j + 1 < nb; ++j) {
            IHit h;
            if (!isect_seg(a[i], a[i+1], b[j], b[j+1], &h)) continue;
            if (h.t < 0.0 || h.t > 1.0 || h.u < 0.0 || h.u > 1.0) {
                set_err(err, errcap,
                        "parameter out of range at segment pair (ia=%d, ib=%d): t=%.17g u=%.17g (need 0..1)",
                        i, j, h.t, h.u);
                return ISECT_ERR_PARAM;
            }
            if (k >= cap) {
                set_err(err, errcap,
                        "capacity %d exceeded at segment pair (ia=%d, ib=%d); %d hits so far",
                        cap, i, j, k);
                return ISECT_ERR_CAP;
            }
            out[k].ia = i; out[k].ib = j;
            out[k].t = h.t; out[k].u = h.u; out[k].at = h.at;
            ++k;
        }
    }
    if (k == 0) {
        *nhits = 0;
        set_err(err, errcap,
                "empty intersection list: 0 hits among %d x %d segment pairs (a[0..%d], b[0..%d])",
                na - 1, nb - 1, na - 1, nb - 1);
        return ISECT_EMPTY;
    }
    if (dedup_sort) {
        qsort(out, (size_t)k, sizeof(IXHit), cmp_hit);
        int w = 1;
        for (int r = 1; r < k; ++r) {
            double sx = 1.0 + fmax(fabs(out[w-1].at.x), fabs(out[r].at.x));
            double sy = 1.0 + fmax(fabs(out[w-1].at.y), fabs(out[r].at.y));
            if (fabs(out[r].at.x - out[w-1].at.x) <= 1e-9 * sx &&
                fabs(out[r].at.y - out[w-1].at.y) <= 1e-9 * sy)
                continue; /* same point reported by neighbouring segment pairs */
            out[w++] = out[r];
        }
        k = w;
    }
    *nhits = k;
    return ISECT_OK;
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
