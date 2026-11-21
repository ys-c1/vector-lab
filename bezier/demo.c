#include "bezier.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#define FINE 4096
#define REPS 10000

static BPoint fine[FINE + 1];

static void derivs(const BPoint c[4], double t, BPoint *d1, BPoint *d2) {
    double u = 1.0 - t;
    BPoint a = {c[1].x - c[0].x, c[1].y - c[0].y};
    BPoint b = {c[2].x - c[1].x, c[2].y - c[1].y};
    BPoint d = {c[3].x - c[2].x, c[3].y - c[2].y};
    d1->x = 3.0 * (u * u * a.x + 2.0 * u * t * b.x + t * t * d.x);
    d1->y = 3.0 * (u * u * a.y + 2.0 * u * t * b.y + t * t * d.y);
    BPoint q0 = {c[2].x - 2.0 * c[1].x + c[0].x,
                 c[2].y - 2.0 * c[1].y + c[0].y};
    BPoint q1 = {c[3].x - 2.0 * c[2].x + c[1].x,
                 c[3].y - 2.0 * c[2].y + c[1].y};
    d2->x = 6.0 * (u * q0.x + t * q1.x);
    d2->y = 6.0 * (u * q0.y + t * q1.y);
}

static double curvature_analytic(const BPoint c[4], double t) {
    BPoint v, a;
    derivs(c, t, &v, &a);
    double cross = v.x * a.y - v.y * a.x;
    double sp = sqrt(v.x * v.x + v.y * v.y);
    return fabs(cross) / (sp * sp * sp);
}

static double curvature_fd(double t) {
    double h = 1.0 / FINE;
    int i = (int)(t * FINE + 0.5);
    BPoint p = fine[i - 1], q = fine[i], r = fine[i + 1];
    BPoint v = { (r.x - p.x) / (2.0 * h), (r.y - p.y) / (2.0 * h) };
    BPoint a = { (r.x - 2.0 * q.x + p.x) / (h * h),
                 (r.y - 2.0 * q.y + p.y) / (h * h) };
    double cross = v.x * a.y - v.y * a.x;
    double sp = sqrt(v.x * v.x + v.y * v.y);
    return fabs(cross) / (sp * sp * sp);
}

static double point_seg_dist(BPoint p, BPoint a, BPoint b) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double wx = p.x - a.x, wy = p.y - a.y;
    double L2 = vx * vx + vy * vy;
    double s = L2 > 0.0 ? (wx * vx + wy * vy) / L2 : 0.0;
    if (s < 0.0) s = 0.0;
    if (s > 1.0) s = 1.0;
    double dx = p.x - (a.x + s * vx), dy = p.y - (a.y + s * vy);
    return sqrt(dx * dx + dy * dy);
}

static double max_deflection(const BPoly *p) {
    double worst = 0.0;
    for (int i = 0; i <= FINE; ++i) {
        double best = 1e300;
        for (int k = 0; k < p->count - 1; ++k) {
            double d = point_seg_dist(fine[i], p->pts[k], p->pts[k + 1]);
            if (d < best) best = d;
        }
        if (best > worst) worst = best;
    }
    return worst;
}

static void uniform_sample(const BPoint c[4], int n, BPoly *p) {
    p->count = n + 1;
    for (int i = 0; i <= n; ++i) p->pts[i] = bezier_eval(c, (double)i / n);
}

static double elapsed_ms(clock_t t0) {
    return 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
}

static void print_segments(const BSegs *s, int base) {
    double h = 1.0 / base;
    for (int k = 0; k < s->count; ++k) {
        printf("  #%-2d interval [%2d,%2d]  t in [%.4f,%.4f]  peak bend %.4f\n",
               k, s->seg[k].i0, s->seg[k].i1,
               s->seg[k].i0 * h, s->seg[k].i1 * h, s->seg[k].bend);
    }
}

int main(void) {
    /* curve with strongly varying curvature: tight at both ends, flat in the middle */
    BPoint c[4] = {{0, 0}, {10, 0}, {70, 0}, {80, 30}};
    for (int i = 0; i <= FINE; ++i) fine[i] = bezier_eval(c, (double)i / FINE);

    /* curvature cross-check: analytic formula vs finite difference */
    printf("curvature check (analytic vs fine-sample finite difference, N=%d):\n", FINE);
    double ts[] = {0.25, 0.5, 0.75};
    for (unsigned i = 0; i < sizeof(ts) / sizeof(ts[0]); ++i) {
        double ka = curvature_analytic(c, ts[i]);
        double kf = curvature_fd(ts[i]);
        printf("  t=%.2f  analytic %.6f  sampled %.6f  rel.err %.2e\n",
               ts[i], ka, kf, fabs(ka - kf) / ka);
    }

    /* threshold = half of the peak |P''| measured on the fine grid */
    double max_bend = 0.0;
    for (int i = 0; i <= FINE; ++i) {
        BPoint v, a;
        derivs(c, (double)i / FINE, &v, &a);
        double m = sqrt(a.x * a.x + a.y * a.y);
        if (m > max_bend) max_bend = m;
    }
    double thr = 0.5 * max_bend;
    printf("threshold = %.4f (0.5 * max |P''| = %.4f), min_len = 2\n\n",
           thr, max_bend);

    BPoly p;
    clock_t t0;

    /* legacy fixed subdivision: adaptive feature disabled, output unchanged */
    printf("== uniform 8 (bezier_flatten, adaptive disabled) ==\n");
    t0 = clock();
    for (int r = 0; r < REPS; ++r) bezier_flatten(c, 0.5, &p);
    double ms8 = elapsed_ms(t0);
    BPoly u8;
    bezier_flatten(c, 0.5, &u8);
    printf("points %d chord %.2f  max deflection %.5f  time %.2f ms / %d calls\n",
           u8.count, bezier_chord(c), max_deflection(&u8), ms8, REPS);

    printf("\n== uniform 64 (fixed sampling reference) ==\n");
    t0 = clock();
    for (int r = 0; r < REPS; ++r) uniform_sample(c, 64, &p);
    double ms64 = elapsed_ms(t0);
    BPoly u64;
    uniform_sample(c, 64, &u64);
    printf("points %d  max deflection %.5f  time %.2f ms / %d calls\n",
           u64.count, max_deflection(&u64), ms64, REPS);

    printf("\n== adaptive, base grid 8 ==\n");
    t0 = clock();
    for (int r = 0; r < REPS; ++r) bezier_adaptive_flatten(c, thr, 8, 2, &p);
    double msa8 = elapsed_ms(t0);
    BPoly a8;
    BSegs seg8;
    if (bezier_adaptive_flatten(c, thr, 8, 2, &a8) != BADAPT_OK ||
        bezier_adaptive_segments(c, thr, 8, 2, &seg8) != BADAPT_OK) return 2;
    printf("segments %d, points %d  max deflection %.5f  time %.2f ms / %d calls\n",
           seg8.count, a8.count, max_deflection(&a8), msa8, REPS);
    print_segments(&seg8, 8);

    printf("\n== adaptive, base grid 64 ==\n");
    t0 = clock();
    for (int r = 0; r < REPS; ++r) bezier_adaptive_flatten(c, thr, 64, 2, &p);
    double msa64 = elapsed_ms(t0);
    BPoly a64;
    BSegs seg64;
    if (bezier_adaptive_flatten(c, thr, 64, 2, &a64) != BADAPT_OK ||
        bezier_adaptive_segments(c, thr, 64, 2, &seg64) != BADAPT_OK) return 2;
    printf("segments %d, points %d  max deflection %.5f  time %.2f ms / %d calls\n",
           seg64.count, a64.count, max_deflection(&a64), msa64, REPS);
    print_segments(&seg64, 64);

    printf("\nsummary:\n");
    printf("  %-22s %8s %14s %12s\n", "case", "segments", "max deflection", "ms/10000");
    printf("  %-22s %8d %14.5f %12.2f\n", "uniform 8", 8, max_deflection(&u8), ms8);
    printf("  %-22s %8d %14.5f %12.2f\n", "uniform 64", 64, max_deflection(&u64), ms64);
    printf("  %-22s %8d %14.5f %12.2f\n", "adaptive base 8", seg8.count,
           max_deflection(&a8), msa8);
    printf("  %-22s %8d %14.5f %12.2f\n", "adaptive base 64", seg64.count,
           max_deflection(&a64), msa64);
    return 0;
}
