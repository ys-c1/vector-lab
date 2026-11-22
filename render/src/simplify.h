#ifndef RSIMPLIFY_H
#define RSIMPLIFY_H
#include "scene.h"

/* Result of simplify_decimate(). */
typedef struct {
    double tolerance;     /* tolerance selected by the bisection */
    double max_deviation; /* measured max deviation of the original path */
    int kept;             /* number of points left in the simplified path */
    int iterations;       /* bisection iterations performed */
} RSimplifyReport;

enum {
    RSIMP_OK = 0,
    RSIMP_ERR_FEW_POINTS = -1, /* fewer than two points (three for a closed ring) */
    RSIMP_ERR_TOLERANCE = -2,  /* negative or non-finite tolerance */
    RSIMP_ERR_ARGUMENT = -3,   /* null pointer / bad arguments */
    RSIMP_ERR_CAPACITY = -4,   /* output buffer too small */
    RSIMP_ERR_BOUND = -5       /* bad deviation bound, or bound cannot be met */
};

/* Simplifies a polyline with Douglas-Peucker at a fixed tolerance.
 *
 * in[0..n-1] is the original path.  If closed is non-zero the path is a ring:
 * there is an implicit segment from in[n-1] back to in[0], and the output
 * does not repeat the first point (a ring keeps at least three points).
 *
 * Kept points are copied from the input in their original order (the output
 * is a subsequence of the input).  tolerance must be finite and >= 0, n must
 * be >= 2 (>= 3 when closed), and cap must have room for the result.
 *
 * On failure a negative RSIMP_* code is returned and, when why is non-NULL,
 * *why points at a static human-readable reason.  On success *out_n is set.
 */
int simplify_points(const RPoint *in, int n, int closed, double tolerance,
                    RPoint *out, int cap, int *out_n, const char **why);

/* Measures how far the original path strays from a simplified path whose
 * points are a subsequence of the original.
 *
 * The result is an upper bound on the one-sided Hausdorff distance from the
 * original polyline to the simplified polyline: every original point lies no
 * farther than the returned value from the simplified path.  It is computed
 * at the original vertices, where the distance to the replacing chord is
 * maximal.  Returns 0 on success and a negative code when simp is not an
 * ordered subsequence of in.
 */
int simplify_deviation(const RPoint *in, int n, int closed,
                       const RPoint *simp, int nsimp, double *out);

/* Reverse-solves the decimation tolerance.
 *
 * Finds, by bisection on the tolerance, a simplification whose measured
 * deviation does not exceed max_deviation while keeping as few points as
 * possible.  The tolerance starts at 0 (never reaches the bound) and is
 * bracketed by the path's bounding-box diagonal (which collapses the path to
 * its minimum point count).  The final result is re-checked; if its measured
 * deviation exceeds max_deviation the call fails with RSIMP_ERR_BOUND.
 *
 * max_deviation must be finite and >= 0.  Validation of n/closed/cap and the
 * rejection reasons are the same as for simplify_points().
 */
int simplify_decimate(const RPoint *in, int n, int closed, double max_deviation,
                      RPoint *out, int cap, RSimplifyReport *report,
                      const char **why);

/* Returns 1 when sub (m points) occurs in in (n points) in the same order,
 * comparing coordinates exactly.  Used to verify order preservation. */
int simplify_is_subsequence(const RPoint *in, int n, const RPoint *sub, int m);

#endif
