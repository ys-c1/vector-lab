#include "isect.h"
#include <math.h>

static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out) {
    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double den = cross2(rx, ry, sx, sy);
    if (fabs(den) < 1e-12) return 0;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    if (out) {
        out->t = t; out->u = u;
        out->at.x = a0.x + rx * t;
        out->at.y = a0.y + ry * t;
    }
    return 1;
}

int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits) {
    if (!a || !b || !hits || na < 2 || nb < 2) return -1;
    int k = 0;
    for (int i = 0; i + 1 < na; ++i)
        for (int j = 0; j + 1 < nb; ++j)
            if (isect_seg(a[i], a[i+1], b[j], b[j+1], 0)) ++k;
    *hits = k;
    return 0;
}

int isect_self(const IPoint *p, int n, int *first_a, int *first_b) {
    if (!p || !first_a || !first_b || n < 4) return -1;
    for (int i = 0; i + 1 < n; ++i) {
        for (int j = i + 2; j + 1 < n; ++j) {
            if (i == 0 && j == n - 2) continue;
            if (isect_seg(p[i], p[i+1], p[j], p[j+1], 0)) {
                *first_a = i; *first_b = j;
                return 1;
            }
        }
    }
    return 0;
}
