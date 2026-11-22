/* Benchmarks the decimation solver on the four path classes the task names:
 * a straight line, a circular arc, a sawtooth, and a closed ring.
 *
 * For each class it reports the input points, the kept points, the measured
 * maximum deviation, the elapsed time, and whether the bound was honoured.
 * Exit status is non-zero if any shape exceeds its bound or loses its order.
 */
#define _POSIX_C_SOURCE 199309L
#include "simplify.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define PI 3.14159265358979323846
#define N 2000

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int gen_line(RPoint *p, int n) {
    for (int i = 0; i < n; ++i) { p[i].x = 10.0 * (double)i; p[i].y = 3.0 * (double)i; }
    return n;
}

static int gen_arc(RPoint *p, int n) {
    for (int i = 0; i < n; ++i) {
        double t = 0.5 * PI * (double)i / (double)(n - 1);
        p[i].x = 500.0 * cos(t);
        p[i].y = 500.0 * sin(t);
    }
    return n;
}

static int gen_saw(RPoint *p, int n) {
    int k = 0, teeth = 50, per = n / 50;
    for (int t = 0; t < teeth && k + per <= n; ++t)
        for (int i = 0; i < per; ++i) {
            p[k].x = 200.0 * (double)t + 4.0 * (double)i;
            p[k].y = (i <= per / 2) ? 40.0 * (double)i / (double)(per / 2)
                                    : 40.0 * (double)(per - i) / (double)(per / 2);
            ++k;
        }
    return k;
}

static int gen_ring(RPoint *p, int n) {
    for (int i = 0; i < n; ++i) {
        double t = 2.0 * PI * (double)i / (double)n;
        double r = 300.0 + 20.0 * sin(7.0 * t);
        p[i].x = 400.0 + r * cos(t);
        p[i].y = 400.0 + r * sin(t);
    }
    return n;
}

typedef int (*Gen)(RPoint *, int);

static int run(const char *name, Gen gen, int closed, double bound) {
    RPoint *in = (RPoint *)malloc(sizeof(RPoint) * N);
    RPoint *out = (RPoint *)malloc(sizeof(RPoint) * N);
    if (!in || !out) { free(in); free(out); return 0; }
    int n = gen(in, N);
    RSimplifyReport rep;
    const char *why = 0;
    double best_ms = -1.0;
    int rc = 0;
    for (int r = 0; r < 3; ++r) {
        double t0 = now_ms();
        rc = simplify_decimate(in, n, closed, bound, out, N, &rep, &why);
        double ms = now_ms() - t0;
        if (rc != 0) break;
        if (best_ms < 0.0 || ms < best_ms) best_ms = ms;
    }
    if (rc != 0) {
        printf("%-6s FAILED: %s (code %d)\n", name, why ? why : "?", rc);
        free(in); free(out);
        return 0;
    }
    int ordered = simplify_is_subsequence(in, n, out, rep.kept);
    double dev = -1.0;
    int drc = simplify_deviation(in, n, closed, out, rep.kept, &dev);
    int ok = ordered && drc == 0 && dev <= bound && rep.max_deviation <= bound;
    printf("%-6s in %4d  kept %4d (%5.1f%%)  maxdev %9.6f  bound %6.2f  %7.3f ms  %s\n",
           name, n, rep.kept, 100.0 * (double)rep.kept / (double)n,
           rep.max_deviation, bound, best_ms, ok ? "ok" : "OVER");
    free(in);
    free(out);
    return ok;
}

int main(void) {
    const double bound = 0.5;
    printf("decimation, bound %.2f units\n", bound);
    int ok = 1;
    ok &= run("line", gen_line, 0, bound);
    ok &= run("arc", gen_arc, 0, bound);
    ok &= run("saw", gen_saw, 0, bound);
    ok &= run("ring", gen_ring, 1, bound);
    return ok ? 0 : 1;
}
