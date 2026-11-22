#include "stroke.h"
#include <math.h>
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* Measured width of the outline: distance between the two offset points
   emitted for segment i. */
static double seg_width(const SPoly *o, int i) {
    double dx = o->pts[2*i].x - o->pts[2*i+1].x;
    double dy = o->pts[2*i].y - o->pts[2*i+1].y;
    return sqrt(dx*dx + dy*dy);
}

/* Hand-computed normal for segment i of (0,0)->(10,0): input unit normal
   is (0,1); for diag(sx,sy) the inverse transpose gives (0,1/sy), which
   normalises to (0,1). */
static int check_hand_normal(const XForm *m, const SPoint *p, int n, double want_w) {
    SPoly o;
    int seg = -1;
    if (stroke_polyline_xform(p, n, 2.0, m, &o, &seg) != STROKE_OK) return 0;
    CK(seg == -1);
    CK(o.count == 2 * n);
    /* Segment 0 (i+1<n) uses p[1]-p[0]; n==2 means segment 1 is the last,
       which reuses the same direction and therefore the same normal. */
    for (int i = 0; i + 1 < n; ++i) {
        double mx = (o.pts[2*i].x + o.pts[2*i+1].x) * 0.5;
        double my = (o.pts[2*i].y + o.pts[2*i+1].y) * 0.5;
        XPoint c = xform_apply(*m, (XPoint){p[i].x, p[i].y});
        CK(fabs(mx - c.x) < 1e-9 && fabs(my - c.y) < 1e-9);
        CK(fabs(o.pts[2*i].x - o.pts[2*i+1].x) < 1e-9);      /* normal is (0,+-1) */
        CK(fabs(seg_width(&o, i) - want_w) <= STROKE_WIDTH_ERR_BOUND);
    }
    return 1;
}

int main(void) {
    SPoint p[3] = {{0,0},{10,0},{10,10}};
    SPoly out;
    CK(stroke_polyline(p, 3, 2.0, &out) == 0);
    CK(out.count == 6);
    CK(out.pts[0].y > 0.9 && out.pts[0].y < 1.1);
    CK(stroke_polyline(p, 1, 2.0, &out) == -1);
    CK(stroke_polyline(p, 3, 0.0, &out) == -1);
    SPoint bad[2] = {{0,0},{0,0}};
    CK(stroke_polyline(bad, 2, 1.0, &out) == -2);

    /* Disabled transform: point-for-point identical to the legacy outline. */
    SPoly legacy, off;
    CK(stroke_polyline(p, 3, 2.0, &legacy) == 0);
    int seg = 7;
    CK(stroke_polyline_xform(p, 3, 2.0, NULL, &off, &seg) == STROKE_OK);
    CK(seg == -1);
    CK(off.count == legacy.count);
    int same = 1;
    for (int i = 0; i < legacy.count; ++i)
        if (off.pts[i].x != legacy.pts[i].x || off.pts[i].y != legacy.pts[i].y) same = 0;
    CK(same);

    /* x-only scale: old code offsets by 0.5*width in the wrong direction;
       the corrected path keeps a full-width, axis-aligned outline. */
    SPoint h[2] = {{0,0},{10,0}};
    XForm sx = {4.0, 0, 0, 1.0, 0, 0};
    CK(check_hand_normal(&sx, h, 2, 2.0));

    /* y-only scale: normal (0,1) -> (0,1/sy), so width must remain 2.0 and
       the offsets must run along y. */
    XForm sy = {1.0, 0, 0, 0.25, 0, 0};
    CK(check_hand_normal(&sy, h, 2, 2.0));

    /* Uniform scale: same answer as the legacy path, mapped. */
    XForm su = {2.0, 0, 0, 2.0, 5.0, 5.0};
    CK(check_hand_normal(&su, h, 2, 2.0));

    /* Zero scale reports the singular transform. */
    XForm zero = {0, 0, 0, 0, 0, 0};
    seg = -1;
    CK(stroke_polyline_xform(h, 2, 2.0, &zero, &out, &seg) == STROKE_ERR_SINGULAR);
    CK(seg == -1);

    /* Zero-length input segment reports its segment index. */
    SPoint badmid[3] = {{0,0},{0,0},{5,0}};
    seg = -1;
    CK(stroke_polyline_xform(badmid, 3, 2.0, &su, &out, &seg) == STROKE_ERR_ZERO_SEG);
    CK(seg == 0);

    /* Degenerate transformed normal: det is 1 (not singular) but the
       inverse transpose maps (0,1) to (0,1e-13), a zero-length normal.
       The segment index must be reported. */
    XForm thin = {1e-13, 0, 0, 1e13, 0, 0};
    seg = -1;
    CK(stroke_polyline_xform(h, 2, 2.0, &thin, &out, &seg) == STROKE_ERR_DEGEN_NORMAL);
    CK(seg == 0);

    /* Near-zero scale is rejected as singular before per-segment work. */
    XForm flat = {1.0, 0, 0, 1e-13, 0, 0};
    seg = -1;
    CK(stroke_polyline_xform(h, 2, 2.0, &flat, &out, &seg) == STROKE_ERR_SINGULAR);
    CK(seg == -1);

    /* Legacy-path zero segment also reports its index through the wrapper. */
    seg = -1;
    CK(stroke_polyline_xform(badmid, 3, 2.0, NULL, &out, &seg) == STROKE_ERR_ZERO_SEG);
    CK(seg == -1); /* NULL path keeps legacy semantics: no segment reported */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
