#include "bezier.h"
#include <math.h>

BPoint bezier_eval(const BPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    BPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

static double dist(BPoint a, BPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

int bezier_flatten(const BPoint c[4], double tol, BPoly *out) {
    if (!out || tol <= 0.0) return -1;
    int n = 8;
    if (n > 63) n = 63;
    out->count = n + 1;
    for (int i = 0; i <= n; ++i) out->pts[i] = bezier_eval(c, (double)i / (double)n);
    (void)dist;
    return 0;
}

double bezier_chord(const BPoint c[4]) { return dist(c[0], c[3]); }
