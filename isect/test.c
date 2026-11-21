#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* same cubic as the implementation, duplicated for independent checks */
static IPoint cubic(const IPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u*u*u, b = 3*u*u*t, d = 3*u*t*t, e = t*t*t;
    IPoint p = { a*c[0].x + b*c[1].x + d*c[2].x + e*c[3].x,
                 a*c[0].y + b*c[1].y + d*c[2].y + e*c[3].y };
    return p;
}

static const IPoint CURVE[4] = {{0,0},{10,30},{20,-30},{30,0}};
static const IPoint LINE[2] = {{-5,-2},{35,-2}};

/* y(t) of CURVE plus 2: roots are the true intersections with LINE */
static double fy(double t) { return 90.0 * t * (1.0 - t) * (1.0 - 2.0 * t) + 2.0; }

/* dense sampling + bisection reference roots, hand-computed from the curve */
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

int main(void) {
    IPoint a0 = {0,0}, a1 = {10,0}, b0 = {5,-5}, b1 = {5,5};
    IHit h;
    CK(isect_seg(a0,a1,b0,b1,&h) == 1);
    CK(h.at.x > 4.9 && h.at.x < 5.1);
    IPoint c0 = {0,1}, c1 = {10,1};
    CK(isect_seg(a0,a1,c0,c1,&h) == 0);
    IPoint pa[2] = {{0,0},{10,0}}, pb[2] = {{5,-5},{5,5}};
    int hits = 0;
    CK(isect_polyline(pa,2,pb,2,&hits) == 0 && hits == 1);
    IPoint bow[5] = {{0,0},{10,10},{10,0},{0,10},{0,0}};
    int ia = -1, ib = -1;
    CK(isect_self(bow,5,&ia,&ib) == 1);
    CK(isect_polyline(pa,1,pb,2,&hits) == -1);

    /* entry validation: bad segments / tol rejected with the field named */
    ICurveOpts opt = {32, 1e-9, 1, 0};
    IHit out[8];
    ICurveOpts bad = opt;
    bad.segments = 0;
    CK(isect_curve_polyline(CURVE, LINE, 2, &bad, out, 8, 0) == ISECT_ERR_SEGMENTS);
    bad.segments = -3;
    CK(isect_curve_polyline(CURVE, LINE, 2, &bad, out, 8, 0) == ISECT_ERR_SEGMENTS);
    CK(strcmp(isect_err_field(ISECT_ERR_SEGMENTS), "segments") == 0);
    bad = opt; bad.tol = 0.0;
    CK(isect_curve_polyline(CURVE, LINE, 2, &bad, out, 8, 0) == ISECT_ERR_TOL);
    bad.tol = -1e-3;
    CK(isect_curve_polyline(CURVE, LINE, 2, &bad, out, 8, 0) == ISECT_ERR_TOL);
    CK(strcmp(isect_err_field(ISECT_ERR_TOL), "tol") == 0);
    bad = opt;
    CK(isect_curve_polyline(CURVE, LINE, 1, &bad, out, 8, 0) == ISECT_ERR_ARG);
    CK(isect_curve_polyline(0, LINE, 2, &bad, out, 8, 0) == ISECT_ERR_ARG);

    /* reference roots from dense sampling */
    double refs[4];
    int nref = ref_roots(refs, 4);
    CK(nref == 2);

    /* refine off: identical to flatten + isect_polyline, computed by hand */
    ICurveOpts off = {32, 1e-9, 0, 0};
    int ngot = isect_curve_polyline(CURVE, LINE, 2, &off, out, 8, 0);
    IHit want[8]; int nwant = 0;
    for (int i = 0; i < 32; ++i) {
        IPoint p0 = cubic(CURVE, i / 32.0), p1 = cubic(CURVE, (i + 1) / 32.0);
        if (isect_seg(p0, p1, LINE[0], LINE[1], &h)) {
            want[nwant].t = (i + h.t) / 32.0;
            want[nwant].u = h.u;
            want[nwant].at = h.at;
            ++nwant;
        }
    }
    CK(ngot == nwant && nwant == 2);
    for (int i = 0; i < nwant && i < ngot; ++i) {
        CK(out[i].t == want[i].t && out[i].u == want[i].u);
        CK(out[i].at.x == want[i].at.x && out[i].at.y == want[i].at.y);
    }

    /* refine on: matches the dense-sampling reference, trace recorded */
    ITrace rows[256];
    ITraceLog log = { rows, 256, 0 };
    ngot = isect_curve_polyline(CURVE, LINE, 2, &opt, out, 8, &log);
    CK(ngot == 2);
    CK(log.count > 0 && log.count <= 256);
    for (int i = 0; i < 2; ++i) {
        CK(fabs(out[i].t - refs[i]) < 1e-6);
        CK(fabs(out[i].at.y - (-2.0)) < 1e-6);
        CK(out[i].u >= 0.0 && out[i].u <= 1.0);
    }
    /* refined hits are strictly closer to the reference than coarse ones */
    ICurveOpts off2 = {32, 1e-9, 0, 0};
    IHit coarse[8];
    CK(isect_curve_polyline(CURVE, LINE, 2, &off2, coarse, 8, 0) == 2);
    for (int i = 0; i < 2; ++i)
        CK(fabs(out[i].t - refs[i]) <= fabs(coarse[i].t - refs[i]) + 1e-15);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
