#ifndef STROKE_H
#define STROKE_H
#include "xform.h"

typedef struct { double x, y; } SPoint;
typedef struct { int count; SPoint pts[128]; } SPoly;

/* Upper bound on |measured width - width| accepted for the transformed
   path (coordinates used by the lab are O(100), so this is generous). */
#define STROKE_WIDTH_ERR_BOUND 1e-9

/* stroke_polyline_xform() result codes. */
#define STROKE_OK               0
#define STROKE_ERR_ARG         -1  /* bad arguments */
#define STROKE_ERR_ZERO_SEG    -2  /* zero-length input segment */
#define STROKE_ERR_SINGULAR    -3  /* transform scale is zero (det == 0) */
#define STROKE_ERR_DEGEN_NORMAL -4 /* transformed normal degenerates to zero */

/* Legacy stroke in input space. */
int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out);

/* Stroke after an affine map.
   The centreline is mapped with `xform`; the unit normal is obtained from
   the inverse transpose (M^-T n) and both sides are offset by width/2 in
   output space, so the output width is uniform under non-uniform scaling.

   xform == NULL disables the transform: the call delegates to
   stroke_polyline() and the outline is point-for-point identical.
   Pass err_seg (may be NULL) to receive the offending segment index on
   STROKE_ERR_ZERO_SEG / STROKE_ERR_DEGEN_NORMAL; it is set to -1 otherwise.
   With xform == NULL the legacy return codes (-1, -2) are returned unchanged
   and err_seg stays -1. */
int stroke_polyline_xform(const SPoint *pts, int n, double width,
                          const XForm *xform, SPoly *out, int *err_seg);
#endif
