#ifndef BEZIER_H
#define BEZIER_H

typedef struct { double x, y; } BPoint;
typedef struct { int count; BPoint pts[65]; } BPoly;

/* adaptive flatten return codes */
enum {
    BADAPT_OK = 0,
    BADAPT_ARG = -1,      /* null pointer or bad base/min_len */
    BADAPT_THRESHOLD = -2,/* threshold <= 0, position reported on stderr */
    BADAPT_DEGEN = -3,    /* degenerate control points, position reported on stderr */
    BADAPT_CAPACITY = -4 /* split result exceeds the 64-point polyline */
};

/* one segment = half-open interval [i0, i1] of the base-grid samples */
typedef struct { int i0, i1; double bend; } BSeg;
typedef struct { int count; BSeg seg[64]; } BSegs;

BPoint bezier_eval(const BPoint c[4], double t);
int bezier_flatten(const BPoint c[4], double tol, BPoly *out);
double bezier_chord(const BPoint c[4]);

/*
 * Entry validation shared by the adaptive entry points.
 * Returns BADAPT_OK / BADAPT_ARG / BADAPT_THRESHOLD / BADAPT_DEGEN and,
 * on failure, prints a message naming the offending control-point index
 * or argument to stderr. base = sample count of the uniform base grid
 * (base+1 samples); min_len = minimum allowed segment length in grid
 * intervals; thr > 0 is the curvature threshold.
 */
int bezier_validate(const BPoint c[4], double thr, int base, int min_len);

/*
 * Curvature-driven subdivision. Second differences of a uniform base
 * grid (normalized by h^2 so the measure is independent of grid
 * spacing), split where bend >= thr, merge neighbouring segments
 * shorter than min_len back together. Writes the resulting polyline
 * (base-grid samples at segment boundaries) to out->pts.
 */
int bezier_adaptive_flatten(const BPoint c[4], double thr, int base,
                            int min_len, BPoly *out);

/* Same subdivision, but reports the interval segmentation itself. */
int bezier_adaptive_segments(const BPoint c[4], double thr, int base,
                             int min_len, BSegs *out);
#endif
