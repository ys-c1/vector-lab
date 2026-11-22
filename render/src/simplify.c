#include "simplify.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const WHY_NULL = "null pointer argument";
static const char *const WHY_FEW_OPEN = "a polyline needs at least 2 points";
static const char *const WHY_FEW_CLOSED = "a closed ring needs at least 3 points";
static const char *const WHY_NEG_TOL = "tolerance is negative";
static const char *const WHY_BAD_TOL = "tolerance is not a finite number";
static const char *const WHY_CAP = "output buffer is too small";
static const char *const WHY_BOUND = "deviation bound could not be met";

static double dist2(RPoint a, RPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return dx * dx + dy * dy;
}

/* Distance from p to segment a-b.  Inside the span this is |cross| / |ab|,
 * which is exactly zero for points that are collinear in floating point (a
 * squared-distance form would report ~1e-15 and defeat a zero tolerance). */
static double dist_point_seg(RPoint p, RPoint a, RPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double len2 = dx * dx + dy * dy;
    if (len2 <= 0.0) return sqrt(dist2(p, a));
    double t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
    if (t <= 0.0) return sqrt(dist2(p, a));
    if (t >= 1.0) return sqrt(dist2(p, b));
    double cross = dx * (p.y - a.y) - dy * (p.x - a.x);
    return fabs(cross) / sqrt(len2);
}

/* Douglas-Peucker over one open chain.  Both ends are always kept; any other
 * point farther than tol from the chord replacing its span is kept as well.
 * A vertex kept by an earlier split is never unkept, so the result is the
 * standard recursive Douglas-Peucker set, computed iteratively. */
static int dp_chain(const RPoint *pts, int n, double tol, unsigned char *keep) {
    if (n <= 0) return 0;
    keep[0] = 1;
    if (n == 1) return 0;
    keep[n - 1] = 1;
    int *stack = (int *)malloc(sizeof(int) * 4 * (size_t)n + 4 * sizeof(int));
    if (!stack) return -1;
    int top = 0;
    stack[top++] = 0;
    stack[top++] = n - 1;
    while (top > 0) {
        int hi = stack[--top];
        int lo = stack[--top];
        int cut = -1;
        double best = 0.0;
        for (int i = lo + 1; i < hi; ++i) {
            double d = dist_point_seg(pts[i], pts[lo], pts[hi]);
            if (d > best) { best = d; cut = i; }
        }
        if (cut >= 0 && best > tol) {
            keep[cut] = 1;
            stack[top++] = lo;
            stack[top++] = cut;
            stack[top++] = cut;
            stack[top++] = hi;
        }
    }
    free(stack);
    return 0;
}

static double bbox_diag(const RPoint *in, int n) {
    double minx = in[0].x, maxx = in[0].x, miny = in[0].y, maxy = in[0].y;
    for (int i = 1; i < n; ++i) {
        if (in[i].x < minx) minx = in[i].x;
        if (in[i].x > maxx) maxx = in[i].x;
        if (in[i].y < miny) miny = in[i].y;
        if (in[i].y > maxy) maxy = in[i].y;
    }
    return sqrt((maxx - minx) * (maxx - minx) + (maxy - miny) * (maxy - miny));
}

static int fail(const char **why, const char *msg, int code) {
    if (why) *why = msg;
    return code;
}

static int check_common(const RPoint *in, const RPoint *out, int n, int closed,
                        double tolerance, int cap, const char **why) {
    if (!in || !out) return fail(why, WHY_NULL, RSIMP_ERR_ARGUMENT);
    if (!isfinite(tolerance))
        return fail(why, tolerance < 0.0 ? WHY_NEG_TOL : WHY_BAD_TOL,
                    RSIMP_ERR_TOLERANCE);
    if (tolerance < 0.0) return fail(why, WHY_NEG_TOL, RSIMP_ERR_TOLERANCE);
    if (n < (closed ? 3 : 2))
        return fail(why, closed ? WHY_FEW_CLOSED : WHY_FEW_OPEN,
                    RSIMP_ERR_FEW_POINTS);
    if (cap < 1) return fail(why, WHY_CAP, RSIMP_ERR_CAPACITY);
    if (why) *why = 0;
    return 0;
}

/* Ensures a closed ring keeps at least three points.  If Douglas-Peucker left
 * only two, add the interior vertex farthest from the chord through them (or,
 * when every interior vertex is on that chord, the first interior vertex). */
static void ring_ensure_three(const RPoint *in, int n, int anchor,
                              unsigned char *keep) {
    int kept = 0;
    for (int i = 0; i < n; ++i) kept += keep[i] != 0;
    if (kept >= 3) return;
    int pick = -1;
    double far = -1.0;
    for (int i = 1; i < n; ++i) {
        if (keep[i]) continue;
        double d = dist_point_seg(in[i], in[0], in[anchor]);
        if (d > far) { far = d; pick = i; }
    }
    if (pick < 0) pick = (n >= 3) ? (anchor == 1 ? 2 : 1) : -1;
    if (pick > 0 && pick < n) keep[pick] = 1;
}

/* Core simplifier.  Writes kept points to out and, when idx is non-NULL, the
 * input index of each kept point; *out_n receives the count. */
static int simplify_impl(const RPoint *in, int n, int closed, double tolerance,
                         RPoint *out, int *idx, int cap, int *out_n,
                         const char **why) {
    unsigned char *keep = (unsigned char *)calloc((size_t)n, 1);
    if (!keep) return fail(why, WHY_CAP, RSIMP_ERR_CAPACITY);
    int rc = 0;
    if (!closed) {
        if (dp_chain(in, n, tolerance, keep) != 0) rc = RSIMP_ERR_CAPACITY;
    } else {
        /* Split the ring at an interior anchor and simplify the two open
         * chains in[0..anchor] and in[anchor..n-1] + in[0].  The closing edge
         * is covered this way, so every ring edge is measured against a
         * replacing chord.  The anchor is the interior vertex farthest from
         * in[0]. */
        int anchor = 1;
        double far = -1.0;
        for (int i = 1; i <= n - 2; ++i) {
            double d = dist2(in[i], in[0]);
            if (d > far) { far = d; anchor = i; }
        }
        RPoint *chain = (RPoint *)malloc(sizeof(RPoint) * (size_t)(n + 1));
        unsigned char *kchain = (unsigned char *)calloc((size_t)(n + 1), 1);
        if (!chain || !kchain) {
            free(chain);
            free(kchain);
            free(keep);
            return fail(why, WHY_CAP, RSIMP_ERR_CAPACITY);
        }
        /* Chain A: in[0..anchor]. */
        for (int i = 0; i <= anchor; ++i) chain[i] = in[i];
        if (dp_chain(chain, anchor + 1, tolerance, kchain) != 0)
            rc = RSIMP_ERR_CAPACITY;
        for (int i = 0; i <= anchor && rc == 0; ++i)
            if (kchain[i]) keep[i] = 1;
        /* Chain B: in[anchor..n-1] then in[0], closing the ring. */
        if (rc == 0) {
            int nb = n - anchor + 1;
            for (int i = 0; i < nb; ++i)
                chain[i] = (anchor + i < n) ? in[anchor + i] : in[0];
            memset(kchain, 0, (size_t)(n + 1));
            if (dp_chain(chain, nb, tolerance, kchain) != 0)
                rc = RSIMP_ERR_CAPACITY;
            for (int i = 0; i < nb && rc == 0; ++i)
                if (kchain[i]) keep[anchor + i < n ? anchor + i : 0] = 1;
        }
        if (rc == 0) ring_ensure_three(in, n, anchor, keep);
        free(chain);
        free(kchain);
    }
    if (rc == 0) {
        int m = 0;
        for (int i = 0; i < n; ++i) if (keep[i]) ++m;
        if (m > cap) {
            rc = fail(why, WHY_CAP, RSIMP_ERR_CAPACITY);
        } else {
            int w = 0;
            for (int i = 0; i < n; ++i) {
                if (!keep[i]) continue;
                out[w] = in[i];
                if (idx) idx[w] = i;
                ++w;
            }
            *out_n = w;
        }
    }
    free(keep);
    return rc;
}

int simplify_points(const RPoint *in, int n, int closed, double tolerance,
                    RPoint *out, int cap, int *out_n, const char **why) {
    if (!out_n) return fail(why, WHY_NULL, RSIMP_ERR_ARGUMENT);
    int rc = check_common(in, out, n, closed, tolerance, cap, why);
    if (rc != 0) return rc;
    int m = 0;
    rc = simplify_impl(in, n, closed, tolerance, out, 0, cap, &m, why);
    if (rc != 0) return rc;
    *out_n = m;
    return RSIMP_OK;
}

int simplify_is_subsequence(const RPoint *in, int n, const RPoint *sub, int m) {
    if (!in || !sub || n < 0 || m < 0) return 0;
    if (m == 0) return 1;
    int j = 0;
    for (int i = 0; i < n && j < m; ++i)
        if (in[i].x == sub[j].x && in[i].y == sub[j].y) ++j;
    return j == m;
}

/* Max distance from the original path to the polyline through simp, where
 * idx gives the input index of every simplified point.  Each original vertex
 * is measured against the simplified segment that replaced its span; a closed
 * ring also measures the vertices before the first kept point against the
 * closing segment.  This is the Douglas-Peucker error of the path. */
static double deviation_indices(const RPoint *in, int n, int closed,
                                const RPoint *simp, int m, const int *idx) {
    double worst = 0.0;
    int spans = closed ? m : m - 1;
    for (int j = 0; j < spans; ++j) {
        int a = idx[j];
        int b = idx[(j + 1) % m];
        RPoint pa = simp[j], pb = simp[(j + 1) % m];
        if (!closed || j + 1 < m) {
            for (int i = a + 1; i < b; ++i) {
                double d = dist_point_seg(in[i], pa, pb);
                if (d > worst) worst = d;
            }
        } else {
            /* Closing span: indices a+1..n-1 and 0..b-1. */
            for (int i = a + 1; i < n; ++i) {
                double d = dist_point_seg(in[i], pa, pb);
                if (d > worst) worst = d;
            }
            for (int i = 0; i < b; ++i) {
                double d = dist_point_seg(in[i], pa, pb);
                if (d > worst) worst = d;
            }
        }
    }
    return worst;
}

int simplify_deviation(const RPoint *in, int n, int closed,
                       const RPoint *simp, int nsimp, double *out) {
    if (!in || !simp || !out) return RSIMP_ERR_ARGUMENT;
    if (nsimp < 1) return RSIMP_ERR_FEW_POINTS;
    if (n < (closed ? 3 : 2) || nsimp < (closed ? 3 : 2)) return RSIMP_ERR_FEW_POINTS;
    if (!simplify_is_subsequence(in, n, simp, nsimp)) return RSIMP_ERR_ARGUMENT;
    int *idx = (int *)malloc(sizeof(int) * (size_t)nsimp);
    if (!idx) return RSIMP_ERR_CAPACITY;
    int j = 0;
    for (int i = 0; i < n && j < nsimp; ++i)
        if (in[i].x == simp[j].x && in[i].y == simp[j].y) idx[j++] = i;
    double dev = (j == nsimp) ? deviation_indices(in, n, closed, simp, nsimp, idx) : 0.0;
    free(idx);
    *out = dev;
    return j == nsimp ? RSIMP_OK : RSIMP_ERR_ARGUMENT;
}

/* Simplifies into scratch (capacity n) and measures the deviation. */
static int try_tolerance(const RPoint *in, int n, int closed, double tol,
                         RPoint *scratch, int *idx, int *kept, double *dev) {
    int m = 0;
    const char *why = 0;
    int rc = simplify_impl(in, n, closed, tol, scratch, idx, n, &m, &why);
    if (rc != 0) return rc;
    *dev = deviation_indices(in, n, closed, scratch, m, idx);
    *kept = m;
    return RSIMP_OK;
}

int simplify_decimate(const RPoint *in, int n, int closed, double max_deviation,
                      RPoint *out, int cap, RSimplifyReport *report,
                      const char **why) {
    int rc = check_common(in, out, n, closed, max_deviation, cap, why);
    if (rc != 0) return rc;
    if (!report) return fail(why, WHY_NULL, RSIMP_ERR_ARGUMENT);

    RPoint *scratch = (RPoint *)malloc(sizeof(RPoint) * (size_t)n);
    int *idx = (int *)malloc(sizeof(int) * (size_t)n);
    if (!scratch || !idx) {
        free(scratch);
        free(idx);
        return fail(why, WHY_CAP, RSIMP_ERR_CAPACITY);
    }

    /* Bisect the tolerance.  0 is always feasible (a vertex is dropped only
     * when it sits exactly on its chord, so the error is 0), and the
     * bounding-box diagonal is the coarsest useful tolerance: an open path
     * collapses to its two ends and a ring to three points.  Keep the
     * feasible sample with the fewest points; on a tie prefer the larger
     * tolerance. */
    double lo = 0.0;
    double hi = bbox_diag(in, n);
    double best_tol = 0.0;
    int best_kept = n + 1;
    int trials = 0;
    double dev = 0.0;
    int kept = 0;

    rc = try_tolerance(in, n, closed, lo, scratch, idx, &kept, &dev);
    if (rc != 0) { free(scratch); free(idx); return rc; }
    best_kept = kept;
    ++trials;

    if (hi > lo) {
        rc = try_tolerance(in, n, closed, hi, scratch, idx, &kept, &dev);
        if (rc != 0) { free(scratch); free(idx); return rc; }
        ++trials;
        if (dev <= max_deviation && kept <= best_kept) {
            best_tol = hi;
            best_kept = kept;
        } else if (dev > max_deviation) {
            for (int it = 0; it < 100; ++it) {
                double mid = lo + 0.5 * (hi - lo);
                if (!(mid > lo) || !(mid < hi)) break;
                rc = try_tolerance(in, n, closed, mid, scratch, idx, &kept, &dev);
                if (rc != 0) { free(scratch); free(idx); return rc; }
                ++trials;
                if (dev <= max_deviation) {
                    lo = mid;
                    if (kept < best_kept || (kept == best_kept && mid > best_tol)) {
                        best_tol = mid;
                        best_kept = kept;
                    }
                } else {
                    hi = mid;
                }
                if (hi - lo <= 1e-12 * (1.0 + fabs(lo) + fabs(hi))) break;
            }
        }
    }

    /* Emit the exact points that will be returned and measure them once
     * more.  Nothing over the bound may leave this function. */
    int m = 0;
    rc = simplify_impl(in, n, closed, best_tol, scratch, idx, n, &m, why);
    if (rc != 0) { free(scratch); free(idx); return rc; }
    double final_dev = deviation_indices(in, n, closed, scratch, m, idx);
    if (final_dev > max_deviation) {
        free(scratch);
        free(idx);
        return fail(why, WHY_BOUND, RSIMP_ERR_BOUND);
    }
    if (m > cap) {
        free(scratch);
        free(idx);
        return fail(why, WHY_CAP, RSIMP_ERR_CAPACITY);
    }
    memcpy(out, scratch, sizeof(RPoint) * (size_t)m);

    report->tolerance = best_tol;
    report->max_deviation = final_dev;
    report->kept = m;
    report->iterations = trials;
    free(scratch);
    free(idx);
    if (why) *why = 0;
    return RSIMP_OK;
}
