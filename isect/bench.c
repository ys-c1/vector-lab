/* bench: curve-vs-polyline two-phase intersection, 32 vs 256 coarse segments.
 * usage: bench [out_dir]   (trace CSVs go to <out_dir>/trace_seg<N>.csv) */
#define _POSIX_C_SOURCE 199309L
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

static const IPoint CURVE[4] = {{0,0},{10,30},{20,-30},{30,0}};
static const IPoint LINE[2] = {{-5,-2},{35,-2}};
#define REPS 20000

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + 1e-9 * ts.tv_nsec;
}

/* y(t) of CURVE plus 2 */
static double fy(double t) { return 90.0 * t * (1.0 - t) * (1.0 - 2.0 * t) + 2.0; }

/* reference intersections: dense sampling (2^16) + bisection, by hand */
static int ref_roots(double *roots, int cap) {
    int n = 0, N = 1 << 16;
    double t0 = 0.0, f0 = fy(0.0);
    for (int i = 1; i <= N && n < cap; ++i) {
        double t1 = i / (double)N, f1 = fy(t1);
        if (f0 * f1 < 0.0) {
            double a = t0, b = t1, fa = f0;
            for (int k = 0; k < 200; ++k) {
                double m = 0.5 * (a + b);
                if (fa * fy(m) <= 0.0) b = m; else { a = m; fa = fy(m); }
            }
            roots[n++] = 0.5 * (a + b);
        }
        t0 = t1; f0 = f1;
    }
    return n;
}

static double err_vs_refs(const IHit *hits, int n, const double *refs, int nref) {
    double worst = 0.0;
    for (int i = 0; i < n; ++i) {
        double best = 1e30;
        for (int j = 0; j < nref; ++j) {
            double d = fabs(hits[i].t - refs[j]);
            if (d < best) best = d;
        }
        if (best > worst) worst = best;
    }
    return worst;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "build";
    double refs[4];
    int nref = ref_roots(refs, 4);
    printf("reference (dense 2^16 sampling + bisection): %d roots\n", nref);
    for (int i = 0; i < nref; ++i) printf("  t = %.15f\n", refs[i]);

    printf("\n| segments | refine | hits | max|dt| vs ref | refine iters | avg us/call |\n");
    printf("|---------:|:------:|-----:|-------------:|-------------:|------------:|\n");

    int segs[2] = {32, 256};
    for (int s = 0; s < 2; ++s) {
        for (int rf = 0; rf <= 1; ++rf) {
            ICurveOpts opt = {segs[s], 1e-9, rf, 0};
            IHit hits[16];
            static ITrace rows[4096];
            ITraceLog log = { rows, 4096, 0 };

            double t0 = now_s();
            int n = 0;
            for (int r = 0; r < REPS; ++r)
                n = isect_curve_polyline(CURVE, LINE, 2, &opt, hits, 16, 0);
            double us = (now_s() - t0) / REPS * 1e6;

            log.count = 0;
            n = isect_curve_polyline(CURVE, LINE, 2, &opt, hits, 16, &log);
            double err = err_vs_refs(hits, n, refs, nref);
            printf("| %8d | %6d | %4d | %12.3e | %12d | %11.3f |\n",
                   segs[s], rf, n, err, rf ? log.count : 0, us);

            if (rf) {
                char path[512];
                snprintf(path, sizeof path, "%s/trace_seg%d.csv", dir, segs[s]);
                if (isect_trace_write(&log, path) == 0)
                    printf("  trace -> %s (%d rows)\n", path, log.count);
                else
                    printf("  trace write FAILED: %s\n", path);
            }
        }
    }
    return 0;
}
