#define _POSIX_C_SOURCE 199309L
#include "bezier.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

static double ms_since(struct timespec t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

int main(void) {
    BPoint c[4] = {{0,0},{20,40},{60,40},{80,0}};
    BPoly p;
    if (bezier_flatten(c, 0.5, &p) != 0) return 1;
    printf("points %d chord %.2f\n", p.count, bezier_chord(c));
    for (int i = 0; i < p.count; ++i) printf("  %.2f %.2f\n", p.pts[i].x, p.pts[i].y);

    /* 新逻辑：由弦高误差上界反解容差，再按容差细分并逐段验证。 */
    printf("\nadaptive: err_bound %.2f -> tol %.6f (4/3 * E)\n",
           0.5, bezier_tol_for_error(0.5));
    BFlat f;
    BPathResult r;
    struct timespec t0;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (bezier_flatten_path((const BPoint (*)[4]) &c, 1, 0.5, &f, &r) != BEZ_OK)
        return 2;
    double flat_ms = ms_since(t0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    if (bezier_verify_path((const BPoint (*)[4]) &c, 1, &f, 0.5, 2048, &r) !=
        BEZ_OK)
        return 3;
    double verify_ms = ms_since(t0);
    printf("pieces %d  tol %.6f  max_err %.6f  %s  flat %.4f ms  verify %.3f ms\n",
           f.pieces, r.tol, r.max_err, r.pass ? "PASS" : "FAIL", flat_ms,
           verify_ms);
    for (int i = 0; i <= f.pieces; ++i)
        printf("  %.2f %.2f\n", f.pts[i].x, f.pts[i].y);
    return r.pass ? 0 : 4;
}
