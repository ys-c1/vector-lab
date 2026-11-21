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

int isect_seg_tol(IPoint a0, IPoint a1, IPoint b0, IPoint b1, double tol, IHit *out) {
    if (tol < 0.0) return -1;
    if (tol == 0.0) return isect_seg(a0, a1, b0, b1, out);
    double rax = a1.x - a0.x, ray = a1.y - a0.y;
    double rsx = b1.x - b0.x, rsy = b1.y - b0.y;
    double la = hypot(rax, ray), lb = hypot(rsx, rsy);
    if (la == 0.0) return -2;
    if (lb == 0.0) return -3;
    double ka = tol / la, kb = tol / lb;
    IPoint ea0 = { a0.x - rax * ka, a0.y - ray * ka };
    IPoint ea1 = { a1.x + rax * ka, a1.y + ray * ka };
    IPoint eb0 = { b0.x - rsx * kb, b0.y - rsy * kb };
    IPoint eb1 = { b1.x + rsx * kb, b1.y + rsy * kb };
    IHit h;
    int r = isect_seg(ea0, ea1, eb0, eb1, &h);
    if (r == 1 && out) {
        /* 扩段参数 t 满足 at = ea0 + t*(ea1-ea0)，换算回原始段参数 */
        out->t = (h.t * (la + 2.0 * tol) - tol) / la;
        out->u = (h.u * (lb + 2.0 * tol) - tol) / lb;
        out->at = h.at;
    }
    return r;
}

int isect_polyline_tol(const IPoint *a, int na, const IPoint *b, int nb,
                       double tol, int *hits, int *err_seg) {
    if (!a || !b || !hits || na < 2 || nb < 2) return -1;
    if (tol < 0.0) return -2;
    if (tol == 0.0) return isect_polyline(a, na, b, nb, hits);
    for (int i = 0; i + 1 < na; ++i)
        if (a[i].x == a[i+1].x && a[i].y == a[i+1].y) {
            if (err_seg) *err_seg = i;
            return -3;
        }
    for (int j = 0; j + 1 < nb; ++j)
        if (b[j].x == b[j+1].x && b[j].y == b[j+1].y) {
            if (err_seg) *err_seg = j;
            return -4;
        }
    int k = 0;
    for (int i = 0; i + 1 < na; ++i)
        for (int j = 0; j + 1 < nb; ++j)
            if (isect_seg_tol(a[i], a[i+1], b[j], b[j+1], tol, 0) == 1) ++k;
    *hits = k;
    return 0;
}
