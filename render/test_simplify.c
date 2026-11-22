#include "src/simplify.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PI 3.14159265358979323846

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int nearly(double a, double b, double eps) { return fabs(a - b) <= eps; }

/* ---- generators ------------------------------------------------------- */

/* A straight horizontal line: every interior point is exactly on the chord. */
static int gen_line(RPoint *p, int n) {
    for (int i = 0; i < n; ++i) { p[i].x = (double)i; p[i].y = 0.0; }
    return n;
}

/* Dense samples of a half circle of radius 100, 721 points. */
static int gen_arc(RPoint *p, int n) {
    int want = 721;
    if (n < want) want = n;
    for (int i = 0; i < want; ++i) {
        double t = PI * (double)i / (double)(want - 1);
        p[i].x = 100.0 * cos(t);
        p[i].y = 100.0 * sin(t);
    }
    return want;
}

/* A sawtooth: 0..10 and back, densely sampled along the ramps. */
static int gen_saw(RPoint *p, int n) {
    int k = 0, teeth = 10;
    for (int t = 0; t < teeth && k + 8 <= n; ++t) {
        for (int i = 0; i < 8; ++i) {
            p[k].x = 20.0 * (double)t + 2.0 * (double)i;
            p[k].y = (i <= 4) ? 10.0 * (double)i / 4.0 : 10.0 * (8 - i) / 4.0;
            ++k;
        }
    }
    return k;
}

/* A dense closed ring: jittered circle, 400 points, not repeating the start. */
static int gen_ring(RPoint *p, int n) {
    int want = 400;
    if (n < want) want = n;
    for (int i = 0; i < want; ++i) {
        double t = 2.0 * PI * (double)i / (double)want;
        double r = 60.0 + 3.0 * sin(5.0 * t);
        p[i].x = 200.0 + r * cos(t);
        p[i].y = 200.0 + r * sin(t);
    }
    return want;
}

/* ---- per-shape checks -------------------------------------------------- */

static void check_shape(const char *name, int closed, int n, const RPoint *in,
                        double bound, int cap) {
    RPoint *out = (RPoint*)malloc(sizeof(RPoint) * (size_t)cap + sizeof(RPoint));
    RSimplifyReport rep;
    const char *why = 0;
    CK(out != 0);
    int rc = simplify_decimate(in, n, closed, bound, out, cap, &rep, &why);
    if (rc != RSIMP_OK) printf("  %s: rc %d why %s\n", name, rc, why ? why : "?");
    CK(rc == RSIMP_OK);
    CK(rep.kept >= (closed ? 3 : 2));
    CK(rep.kept <= n);
    CK(rep.max_deviation <= bound);
    /* Order: output must be an ordered subsequence of the input. */
    CK(simplify_is_subsequence(in, n, out, rep.kept));
    /* Independent re-measure, from scratch, must agree with the report. */
    double dev = -1.0;
    CK(simplify_deviation(in, n, closed, out, rep.kept, &dev) == RSIMP_OK);
    CK(nearly(dev, rep.max_deviation, 1e-12));
    CK(dev <= bound);
    /* Endpoints survive. */
    CK(out[0].x == in[0].x && out[0].y == in[0].y);
    if (!closed)
        CK(out[rep.kept - 1].x == in[n - 1].x && out[rep.kept - 1].y == in[n - 1].y);
    printf("  %-8s in %3d kept %3d tol %8.4f dev %8.6f bound %5.1f iters %d\n",
           name, n, rep.kept, rep.tolerance, rep.max_deviation, bound, rep.iterations);
    free(out);
}

int main(void) {
    enum { N = 4096 };
    RPoint *in = (RPoint*)malloc(sizeof(RPoint) * N);
    CK(in != 0);

    /* Straight line: all interior points are collinear, nothing to keep. */
    int n = gen_line(in, 1000);
    RPoint out[8];
    int m = 0;
    const char *why = 0;
    CK(simplify_points(in, n, 0, 0.0, out, 6, &m, &why) == RSIMP_OK);
    CK(m == 2);
    CK(out[0].x == in[0].x && out[1].x == in[n - 1].x);
    CK(simplify_is_subsequence(in, n, out, m));

    /* The four path classes, each with a bound well under the sag. */
    printf("decimation:\n");
    n = gen_line(in, 1000);
    check_shape("line", 0, n, in, 0.25, N);
    n = gen_arc(in, N);
    check_shape("arc", 0, n, in, 0.25, N);
    n = gen_saw(in, N);
    check_shape("saw", 0, n, in, 0.5, N);
    n = gen_ring(in, N);
    check_shape("ring", 1, n, in, 0.5, N);

    /* A looser bound must never keep more points than a tighter one. */
    n = gen_arc(in, N);
    RPoint *tight = (RPoint*)malloc(sizeof(RPoint) * N);
    RPoint *loose = (RPoint*)malloc(sizeof(RPoint) * N);
    RSimplifyReport rt, rl;
    CK(simplify_decimate(in, n, 0, 0.05, tight, N, &rt, &why) == RSIMP_OK);
    CK(simplify_decimate(in, n, 0, 1.0, loose, N, &rl, &why) == RSIMP_OK);
    CK(rl.kept <= rt.kept);
    CK(rl.max_deviation <= 1.0);
    free(tight);
    free(loose);

    /* A bound of 0 on a sawtooth cannot keep every vertex: the scene cap is
     * the caller's limit and must be reported, not silently truncated. */
    n = gen_saw(in, N);
    CK(simplify_decimate(in, n, 0, 0.0, out, 8, &rt, &why) == RSIMP_ERR_CAPACITY);

    /* ---- rejections ---- */
    RPoint two[2] = {{0,0},{1,0}};
    CK(simplify_points(two, 2, 0, -1.0, out, 8, &m, &why) == RSIMP_ERR_TOLERANCE);
    CK(why != 0);
    printf("  reject(%s)\n", why);
    why = 0;
    CK(simplify_decimate(two, 2, 0, -0.5, out, 8, &rt, &why) == RSIMP_ERR_TOLERANCE);
    CK(why != 0);
    printf("  reject(%s)\n", why);
    why = 0;
    CK(simplify_points(two, 1, 0, 0.0, out, 8, &m, &why) == RSIMP_ERR_FEW_POINTS);
    CK(why != 0);
    printf("  reject(%s)\n", why);
    why = 0;
    CK(simplify_points(two, 0, 0, 0.0, out, 8, &m, &why) == RSIMP_ERR_FEW_POINTS);
    CK(why != 0);
    why = 0;
    RPoint tri[3] = {{0,0},{1,0},{0,1}};
    CK(simplify_points(tri, 2, 1, 0.0, out, 8, &m, &why) == RSIMP_ERR_FEW_POINTS);
    CK(why != 0);
    printf("  reject(%s)\n", why);
    why = 0;
    CK(simplify_decimate(tri, 3, 1, NAN, out, 8, &rt, &why) == RSIMP_ERR_TOLERANCE);
    CK(why != 0);
    why = 0;
    CK(simplify_points(two, 2, 0, 0.0, out, 1, &m, &why) == RSIMP_ERR_CAPACITY);
    CK(why != 0);
    printf("  reject(%s)\n", why);
    why = 0;
    CK(simplify_points(0, 2, 0, 0.0, out, 8, &m, &why) == RSIMP_ERR_ARGUMENT);
    CK(why != 0);

    /* ---- deviation helper contract ---- */
    RPoint line6[4] = {{0,0},{1,0.1},{2,-0.1},{3,0}};
    RPoint line2[2] = {{0,0},{3,0}};
    double dev = -1.0;
    CK(simplify_deviation(line6, 4, 0, line2, 2, &dev) == RSIMP_OK);
    CK(nearly(dev, 0.1, 1e-12));
    RPoint swapped[2] = {{3,0},{0,0}};
    CK(simplify_deviation(line6, 4, 0, swapped, 2, &dev) == RSIMP_ERR_ARGUMENT);
    RPoint foreign[2] = {{0,0},{2,0}};
    CK(simplify_deviation(line6, 4, 0, foreign, 2, &dev) == RSIMP_ERR_ARGUMENT);
    CK(simplify_is_subsequence(line6, 4, line2, 2) == 1);
    CK(simplify_is_subsequence(line6, 4, swapped, 2) == 0);

    free(in);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
