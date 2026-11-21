#ifndef CLIP_H
#define CLIP_H

/* Maximum number of holes accepted by clip_convex_holes(). */
#define CLIP_MAX_HOLES 32
/* Maximum number of outer rings / hole rings in a result. */
#define CLIP_MAX_RINGS 32
/* Maximum number of vertices in one result ring. */
#define CLIP_RING_CAP 256

typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[64]; } CPoly;

/* A result ring.  Outer rings are CCW, hole rings are CW, so the signed
 * area of a hole ring is negative. */
typedef struct { int count; CPoint pts[CLIP_RING_CAP]; } CRing;

typedef struct {
    int outer_count;
    int hole_count;
    CRing outer[CLIP_MAX_RINGS];
    CRing hole[CLIP_MAX_RINGS];
} CPolyRings;

enum {
    CLIP_OK = 0,
    CLIP_ERR_ARGS = -1,
    CLIP_ERR_HOLE_DEGENERATE = -2,
    CLIP_ERR_HOLE_OUTSIDE = -3,
    CLIP_ERR_CAPACITY = -4,
    CLIP_ERR_INTERNAL = -5
};


int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);
double clip_area(const CPoly *p);

/* Validate one hole ring against the outer window.
 * Returns CLIP_OK, CLIP_ERR_HOLE_DEGENERATE or CLIP_ERR_HOLE_OUTSIDE. */
int clip_hole_status(const CPoint *win, int wn, const CPoint *hole, int hn);

/* Clip a polygon against a window made of an outer ring plus holes.
 *
 * The subject is first clipped by the outer ring, then every hole is
 * subtracted (the hole boundary, reversed, is merged into the result).
 * With nhole == 0 the result is point-for-point identical to clip_convex().
 *
 * On a bad hole the function returns one of the CLIP_ERR_HOLE_* codes and
 * writes the hole index to *bad_hole (when bad_hole is non-NULL). */
int clip_convex_holes(const CPoint *poly, int n,
                      const CPoint *win, int wn,
                      const CPoly *holes, int nhole,
                      CPolyRings *out, int *bad_hole);

/* Signed area of all result rings: outer rings add, hole rings subtract. */
double clip_rings_area(const CPolyRings *r);

#endif
