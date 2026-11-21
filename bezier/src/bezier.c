#include "bezier.h"
#include <math.h>
#include <stdio.h>

#define BASE_MAX 64

BPoint bezier_eval(const BPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    BPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

static double dist(BPoint a, BPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

int bezier_flatten(const BPoint c[4], double tol, BPoly *out) {
    if (!out || tol <= 0.0) return -1;
    int n = 8;
    if (n > 63) n = 63;
    out->count = n + 1;
    for (int i = 0; i <= n; ++i) out->pts[i] = bezier_eval(c, (double)i / (double)n);
    (void)dist;
    return 0;
}

double bezier_chord(const BPoint c[4]) { return dist(c[0], c[3]); }

static int finite_pt(BPoint p) {
    return isfinite(p.x) && isfinite(p.y);
}

int bezier_validate(const BPoint c[4], double thr, int base, int min_len) {
    if (!c) {
        fprintf(stderr, "bezier adaptive: null control-point array\n");
        return BADAPT_ARG;
    }
    if (base < 2 || base > BASE_MAX || min_len < 1) {
        fprintf(stderr,
                "bezier adaptive: bad arguments (base=%d, min_len=%d); "
                "need 2 <= base <= %d, min_len >= 1\n",
                base, min_len, BASE_MAX);
        return BADAPT_ARG;
    }
    if (!isfinite(thr) || thr <= 0.0) {
        fprintf(stderr,
                "bezier adaptive: threshold must be positive and finite "
                "(got %.6g)\n", thr);
        return BADAPT_THRESHOLD;
    }
    for (int i = 0; i < 4; ++i) {
        if (!finite_pt(c[i])) {
            fprintf(stderr,
                    "bezier adaptive: control point %d has a non-finite "
                    "coordinate (degenerate)\n", i);
            return BADAPT_DEGEN;
        }
    }
    for (int i = 0; i < 3; ++i) {
        if (dist(c[i], c[i + 1]) == 0.0) {
            fprintf(stderr,
                    "bezier adaptive: control points %d and %d coincide "
                    "(degenerate consecutive pair)\n", i, i + 1);
            return BADAPT_DEGEN;
        }
    }
    if (dist(c[0], c[3]) == 0.0) {
        fprintf(stderr,
                "bezier adaptive: control points 0 and 3 coincide "
                "(zero-length curve, degenerate)\n");
        return BADAPT_DEGEN;
    }
    return BADAPT_OK;
}

/*
 * Shared subdivision core. Returns segment count (>= 1) or a negative
 * BADAPT_* code. On success, bnd[0..nseg] holds base-grid boundary
 * indices and peak[0..nseg-1] the per-segment peak bend.
 */
static int subdivide(const BPoint c[4], double thr, int base, int min_len,
                     int *bnd, double *peak) {
    int rc = bezier_validate(c, thr, base, min_len);
    if (rc != BADAPT_OK) return rc;

    BPoint s[BASE_MAX + 1];
    double bend[BASE_MAX + 1];
    double h = 1.0 / (double)base;
    for (int i = 0; i <= base; ++i) s[i] = bezier_eval(c, (double)i * h);

    /* second difference, normalized by h^2 to stay grid-independent */
    bend[0] = 0.0;
    bend[base] = 0.0;
    double h2 = h * h;
    for (int i = 1; i < base; ++i) {
        double ddx = s[i - 1].x - 2.0 * s[i].x + s[i + 1].x;
        double ddy = s[i - 1].y - 2.0 * s[i].y + s[i + 1].y;
        bend[i] = sqrt(ddx * ddx + ddy * ddy) / h2;
    }

    /* split at every interior vertex whose bend reaches the threshold */
    int nseg = 1;
    bnd[0] = 0;
    for (int i = 1; i < base; ++i) {
        if (bend[i] >= thr) bnd[nseg++] = i;
    }
    bnd[nseg] = base;

    /* peak bend of each segment, including its boundary vertices */
    for (int k = 0; k < nseg; ++k) {
        double m = 0.0;
        for (int i = bnd[k]; i <= bnd[k + 1]; ++i)
            if (bend[i] > m) m = bend[i];
        peak[k] = m;
    }

    /*
     * Merge over-short segments back together. Each round absorbs the
     * first segment below min_len into the neighbour that gives the
     * best result: shorter side first (keeps the other side growing),
     * then lower peak bend, leftmost on a full tie.
     */
    while (nseg > 1) {
        int k = 0;
        while (k < nseg && bnd[k + 1] - bnd[k] >= min_len) ++k;
        if (k == nseg) break;

        int pick;
        if (k == 0) {
            pick = 1;
        } else if (k == nseg - 1) {
            pick = k - 1;
        } else {
            int llen = bnd[k] - bnd[k - 1];
            int rlen = bnd[k + 2] - bnd[k + 1];
            if (llen != rlen) {
                pick = llen < rlen ? k - 1 : k + 1;
            } else if (peak[k - 1] != peak[k + 1]) {
                pick = peak[k - 1] < peak[k + 1] ? k - 1 : k + 1;
            } else {
                pick = k - 1;
            }
        }

        int lo = k < pick ? k : pick;
        int hi = lo + 1;
        bnd[lo + 1] = bnd[hi + 1];
        double m = peak[k] > peak[pick] ? peak[k] : peak[pick];
        for (int j = hi + 1; j <= nseg; ++j) bnd[j - 1] = bnd[j];
        for (int j = hi; j < nseg; ++j) peak[j - 1] = peak[j];
        peak[lo] = m;
        --nseg;
    }
    return nseg;
}

int bezier_adaptive_flatten(const BPoint c[4], double thr, int base,
                            int min_len, BPoly *out) {
    if (!out) {
        fprintf(stderr, "bezier adaptive: null output polyline\n");
        return BADAPT_ARG;
    }
    int bnd[BASE_MAX + 2];
    double peak[BASE_MAX + 1];
    int nseg = subdivide(c, thr, base, min_len, bnd, peak);
    if (nseg < 0) return nseg;
    if (nseg + 1 > (int)(sizeof(out->pts) / sizeof(out->pts[0]))) {
        fprintf(stderr,
                "bezier adaptive: %d segments need %d points, capacity is %d\n",
                nseg, nseg + 1, (int)(sizeof(out->pts) / sizeof(out->pts[0])));
        return BADAPT_CAPACITY;
    }
    double h = 1.0 / (double)base;
    out->count = nseg + 1;
    for (int k = 0; k <= nseg; ++k)
        out->pts[k] = bezier_eval(c, (double)bnd[k] * h);
    return BADAPT_OK;
}

int bezier_adaptive_segments(const BPoint c[4], double thr, int base,
                             int min_len, BSegs *out) {
    if (!out) {
        fprintf(stderr, "bezier adaptive: null segment output\n");
        return BADAPT_ARG;
    }
    int bnd[BASE_MAX + 2];
    double peak[BASE_MAX + 1];
    int nseg = subdivide(c, thr, base, min_len, bnd, peak);
    if (nseg < 0) return nseg;
    if (nseg > (int)(sizeof(out->seg) / sizeof(out->seg[0])))
        return BADAPT_CAPACITY;
    out->count = nseg;
    for (int k = 0; k < nseg; ++k) {
        out->seg[k].i0 = bnd[k];
        out->seg[k].i1 = bnd[k + 1];
        out->seg[k].bend = peak[k];
    }
    return BADAPT_OK;
}
