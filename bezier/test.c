#include "bezier.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static BPoint fd_curvature(const BPoint c[4], double t, int n) {
    double h = 1.0 / n;
    BPoint p = bezier_eval(c, t - h);
    BPoint q = bezier_eval(c, t);
    BPoint r = bezier_eval(c, t + h);
    BPoint v = { (r.x - p.x) / (2.0 * h), (r.y - p.y) / (2.0 * h) };
    BPoint a = { (r.x - 2.0 * q.x + p.x) / (h * h),
                 (r.y - 2.0 * q.y + p.y) / (h * h) };
    (void)v;
    return a;
}

int main(void) {
    BPoint c[4] = {{0,0},{0,10},{10,10},{10,0}};
    BPoint m = bezier_eval(c, 0.5);
    CK(m.x > 4.9 && m.x < 5.1);
    CK(m.y > 7.4 && m.y < 7.6);
    BPoly p;
    CK(bezier_flatten(c, 0.25, &p) == 0);
    CK(p.count == 9);
    CK(bezier_chord(c) > 9.9 && bezier_chord(c) < 10.1);
    CK(bezier_flatten(c, 0.0, &p) == -1);

    /* ---- adaptive entry validation ---- */
    CK(bezier_adaptive_flatten(c, 0.0, 8, 2, &p) == BADAPT_THRESHOLD);
    CK(bezier_adaptive_flatten(c, -1.0, 8, 2, &p) == BADAPT_THRESHOLD);
    BSegs stmp;
    CK(bezier_adaptive_segments(c, NAN, 8, 2, &stmp) == BADAPT_THRESHOLD);
    CK(bezier_adaptive_flatten(c, 1.0, 1, 2, &p) == BADAPT_ARG);
    CK(bezier_adaptive_flatten(c, 1.0, 8, 0, &p) == BADAPT_ARG);
    CK(bezier_adaptive_flatten(0, 1.0, 8, 2, &p) == BADAPT_ARG);
    CK(bezier_adaptive_flatten(c, 1.0, 8, 2, 0) == BADAPT_ARG);
    CK(bezier_adaptive_segments(c, 1.0, 8, 2, 0) == BADAPT_ARG);

    BPoint bad_nan[4] = {{0,0},{0,10},{10, NAN},{10,0}};
    CK(bezier_adaptive_flatten(bad_nan, 1.0, 8, 2, &p) == BADAPT_DEGEN);

    BPoint coincident[4] = {{0,0},{0,10},{0,10},{10,0}}; /* pts 1 == 2 */
    CK(bezier_adaptive_flatten(coincident, 1.0, 8, 2, &p) == BADAPT_DEGEN);

    BPoint closed[4] = {{0,0},{0,10},{10,10},{0,0}}; /* endpoints coincide */
    CK(bezier_adaptive_flatten(closed, 1.0, 8, 2, &p) == BADAPT_DEGEN);

    /* ---- adaptive sanity on the standard curve ---- */
    BSegs s8, s64;
    CK(bezier_adaptive_segments(c, 50.0, 8, 2, &s8) == BADAPT_OK);
    CK(bezier_adaptive_segments(c, 50.0, 64, 2, &s64) == BADAPT_OK);
    CK(s8.count >= 1 && s8.count <= 8);
    CK(s64.count >= 1 && s64.count <= 64);
    /* interval numbering must partition [0, base] */
    CK(s8.seg[0].i0 == 0 && s8.seg[s8.count - 1].i1 == 8);
    CK(s64.seg[0].i0 == 0 && s64.seg[s64.count - 1].i1 == 64);
    for (int k = 1; k < s8.count; ++k)
        CK(s8.seg[k].i0 == s8.seg[k - 1].i1);
    for (int k = 1; k < s64.count; ++k)
        CK(s64.seg[k].i0 == s64.seg[k - 1].i1);
    /* no under-length segment survives the merge-back */
    for (int k = 0; k < s8.count; ++k)
        CK(s8.seg[k].i1 - s8.seg[k].i0 >= 2);
    for (int k = 0; k < s64.count; ++k)
        CK(s64.seg[k].i1 - s64.seg[k].i0 >= 2);

    /* flatten output agrees with the reported segmentation */
    CK(bezier_adaptive_flatten(c, 50.0, 8, 2, &p) == BADAPT_OK);
    CK(p.count == s8.count + 1);
    for (int k = 0; k <= s8.count; ++k) {
        int idx = k == s8.count ? s8.seg[k - 1].i1 : s8.seg[k].i0;
        BPoint ref = bezier_eval(c, (double)idx / 8.0);
        CK(fabs(p.pts[k].x - ref.x) < 1e-12 &&
           fabs(p.pts[k].y - ref.y) < 1e-12);
    }

    /* finer threshold-independent check: threshold above max bend -> 1 segment */
    CK(bezier_adaptive_segments(c, 1e9, 8, 2, &s8) == BADAPT_OK);
    CK(s8.count == 1 && s8.seg[0].i0 == 0 && s8.seg[0].i1 == 8);

    /* ---- second difference vs analytic second derivative cross-check ---- */
    BPoint curve[4] = {{0,0},{20,40},{60,40},{80,0}};
    int base = 64, h_n = 4096;
    double h = 1.0 / base;
    double max_rel = 0.0;
    for (int i = 1; i < base; ++i) {
        double t = (double)i * h;
        BPoint q = bezier_eval(curve, t - h);
        BPoint r = bezier_eval(curve, t);
        BPoint u = bezier_eval(curve, t + h);
        double ddx = (q.x - 2.0 * r.x + u.x) / (h * h);
        double ddy = (q.y - 2.0 * r.y + u.y) / (h * h);
        BPoint a = fd_curvature(curve, t, h_n);
        double rel = fabs(sqrt(ddx * ddx + ddy * ddy) -
                          sqrt(a.x * a.x + a.y * a.y)) /
                     sqrt(a.x * a.x + a.y * a.y);
        if (rel > max_rel) max_rel = rel;
    }
    CK(max_rel < 1e-3);

    /* straight (collinear) curve is not degenerate: one segment, zero bend */
    BPoint line[4] = {{0,0},{1,1},{2,2},{3,3}};
    CK(bezier_adaptive_segments(line, 1.0, 8, 2, &s8) == BADAPT_OK);
    CK(s8.count == 1 && s8.seg[0].bend == 0.0);
    CK(bezier_adaptive_flatten(line, 1.0, 8, 2, &p) == BADAPT_OK);
    CK(p.count == 2);

    /* adaptive polyline endpoints always coincide with the curve endpoints */
    CK(bezier_adaptive_flatten(c, 50.0, 64, 2, &p) == BADAPT_OK);
    CK(fabs(p.pts[0].x - c[0].x) < 1e-12);
    CK(fabs(p.pts[p.count - 1].x - c[3].x) < 1e-12);

    /* min_len=1: nothing is merged back, every split survives */
    CK(bezier_adaptive_segments(c, 50.0, 8, 1, &s8) == BADAPT_OK);
    for (int k = 0; k < s8.count; ++k) CK(s8.seg[k].i1 - s8.seg[k].i0 >= 1);
    /* min_len larger than the whole grid: collapses to a single segment */
    CK(bezier_adaptive_segments(c, 50.0, 8, 9, &s8) == BADAPT_OK);
    CK(s8.count == 1 && s8.seg[0].i0 == 0 && s8.seg[0].i1 == 8);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
