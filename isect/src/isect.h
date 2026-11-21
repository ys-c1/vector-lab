#ifndef ISECT_H
#define ISECT_H
typedef struct { double x, y; } IPoint;
typedef struct { int count; IPoint pts[128]; } IPoly;
typedef struct { double t; double u; IPoint at; } IHit;
int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out);
int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits);
int isect_self(const IPoint *p, int n, int *first_a, int *first_b);

/* ---- cubic bezier curve vs polyline: coarse subdivide + local refine ---- */

#define ISECT_OK 0
#define ISECT_ERR_ARG      (-1)  /* null pointer / bad counts */
#define ISECT_ERR_SEGMENTS (-2)  /* opts->segments <= 0 */
#define ISECT_ERR_TOL      (-3)  /* opts->tol <= 0 or NaN */

typedef struct {
    int segments;   /* coarse subdivision of the curve; must be > 0 */
    double tol;     /* refine stop tolerance (distance to target segment); must be > 0 */
    int refine;     /* 0: coarse only, hits identical to flatten + isect_polyline;
                       1: refine each coarse bracket on the true curve */
    int max_iter;   /* per-bracket refine iteration cap; <= 0 selects default (64) */
} ICurveOpts;

typedef struct {
    int hit;        /* index of the coarse hit being refined */
    int step;       /* refine iteration within the bracket (1-based) */
    double t0, t1;  /* curve-parameter bracket before this step */
    double tm;      /* probe parameter */
    IPoint at;      /* curve point at tm */
    double resid;   /* distance from at to the target segment's line */
} ITrace;

typedef struct {
    ITrace *rows;   /* caller-owned storage, may be NULL to skip recording */
    int cap;        /* capacity of rows */
    int count;      /* rows appended (== refine iterations recorded) */
} ITraceLog;

/* field name responsible for a negative return code, NULL if none */
const char *isect_err_field(int code);

/* Intersect cubic bezier c[4] with polyline (poly, np).
 * Returns number of hits (>= 0, at most out_cap) or a negative ISECT_ERR_*.
 * out[i].t is the curve parameter in [0,1]; out[i].u is the polyline
 * parameter (segment index + fraction); out[i].at is the hit point.
 * With opts->refine == 0 the hits are exactly those of intersecting the
 * opts->segments flattening of the curve, as isect_polyline would report. */
int isect_curve_polyline(const IPoint c[4], const IPoint *poly, int np,
                         const ICurveOpts *opts, IHit *out, int out_cap,
                         ITraceLog *trace);

/* write trace rows as CSV (hit,step,t0,t1,tm,x,y,resid); 0 on success */
int isect_trace_write(const ITraceLog *log, const char *path);
#endif
