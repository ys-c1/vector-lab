#define _POSIX_C_SOURCE 199309L
#include "stroke.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#define ITERS 200000

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e6 + (double)ts.tv_nsec * 1e-3;
}

/* Reference normal computed independently: map the tangent with the linear
   part of M, take its perpendicular, and normalise. */
static void ref_dir(const XForm *m, double dx, double dy, double *ux, double *uy) {
    double tx = m->a * dx + m->c * dy;
    double ty = m->b * dx + m->d * dy;
    double len = sqrt(tx * tx + ty * ty);
    *ux = -ty / len;
    *uy = tx / len;
}

static void report(const char *label, const SPoint *pts, int n, double width,
                   const XForm *m) {
    SPoly out;
    int seg = -1;
    int rc = stroke_polyline_xform(pts, n, width, m, &out, &seg);
    if (rc != STROKE_OK) {
        printf("%s: error %d at segment %d\n", label, rc, seg);
        return;
    }
    printf("%s  width=%.2f  det=%.3f  bound=%.0e\n", label, width,
           xform_det(*m), STROKE_WIDTH_ERR_BOUND);
    printf("  %3s  %9s  %11s  %10s\n", "seg", "width", "width_err", "time_us");

    double max_err = 0.0, max_side_err = 0.0, total_us = 0.0;
    for (int i = 0; i < n; ++i) {
        double dx, dy;
        if (i + 1 < n) { dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y; }
        else { dx = pts[i].x - pts[i-1].x; dy = pts[i].y - pts[i-1].y; }

        /* Measured width and direction of this segment's outline pair. */
        double ox = out.pts[2*i].x - out.pts[2*i+1].x;
        double oy = out.pts[2*i].y - out.pts[2*i+1].y;
        double w = sqrt(ox * ox + oy * oy);
        double err = fabs(w - width);
        if (err > max_err) max_err = err;

        /* Cross-check both sides against the hand-derived reference normal:
           each offset point must sit exactly width/2 from the mapped centre
           in the direction mu. */
        double ux, uy;
        ref_dir(m, dx, dy, &ux, &uy);
        XPoint c = xform_apply(*m, (XPoint){pts[i].x, pts[i].y});
        double h = width * 0.5;
        double ea = hypot(out.pts[2*i].x - (c.x + ux * h),
                          out.pts[2*i].y - (c.y + uy * h));
        double eb = hypot(out.pts[2*i+1].x - (c.x - ux * h),
                          out.pts[2*i+1].y - (c.y - uy * h));
        if (ea > max_side_err) max_side_err = ea;
        if (eb > max_side_err) max_side_err = eb;

        /* Per-segment timing: stroke this one segment alone. */
        SPoint one[2] = {{pts[i].x, pts[i].y}, {pts[i].x + dx, pts[i].y + dy}};
        double t0 = now_us();
        int sink = 0;
        for (int r = 0; r < ITERS; ++r) {
            SPoly tmp;
            if (stroke_polyline_xform(one, 2, width, m, &tmp, 0) == STROKE_OK)
                sink += tmp.count;
        }
        double us = (now_us() - t0) / (double)ITERS;
        if (sink == 1234567) printf(" ");
        total_us += us;

        printf("  %3d  %9.5f  %11.3e  %10.3f\n", i, w, err, us);
    }
    printf("  max width error %.3e  (bound %.0e)  max side error %.3e  total %.3f us\n\n",
           max_err, STROKE_WIDTH_ERR_BOUND, max_side_err, total_us);
}

int main(void) {
    /* Parallelogram with diagonal edges, so the transformed normals are not
       axis-aligned and the width error against the analytic value is
       exercised, not just the trivial axis case. */
    SPoint p[4] = {{0,0},{40,0},{50,20},{10,20}};
    double width = 3.0;

    /* Two non-uniform scale ratios, on different axes. Without the inverse
       transpose the outline is 4x too wide on one side and 1x on the other. */
    XForm m1 = {4.0, 0.0, 0.0, 1.0, 0.0, 0.0};
    XForm m2 = {1.0, 0.0, 0.0, 0.25, 0.0, 0.0};

    report("case A  scale (4.00, 1.00)", p, 4, width, &m1);
    report("case B  scale (1.00, 0.25)", p, 4, width, &m2);

    /* Translate does not change the normal; included as a control. */
    XForm m3 = {4.0, 0.0, 0.0, 1.0, 7.0, -3.0};
    report("case C  scale (4.00, 1.00) + translate", p, 4, width, &m3);
    return 0;
}
