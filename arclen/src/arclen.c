#include "arclen.h"
#include <math.h>

static double seg_len(APoint a, APoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

int arclen_build(const APoint *pts, int n, ATable *out) {
    if (!pts || !out || n < 2 || n > 64) return -1;
    out->count = n;
    out->s[0] = 0.0;
    for (int i = 1; i < n; ++i)
        out->s[i] = out->s[i-1] + seg_len(pts[i-1], pts[i]);
    return 0;
}

double arclen_total(const ATable *t, const APoint *pts, int n) {
    if (!t || !pts || n < 2) return -1.0;
    (void)pts;
    return t->s[n-1];
}

int arclen_sample_at(const APoint *pts, int n, const ATable *t, double s, APoint *out) {
    if (!pts || !t || !out || n < 2) return -1;
    if (s < 0.0 || s > t->s[n-1]) return -2;
    int i = 0;
    while (i + 2 < n && t->s[i+1] < s) ++i;
    double span = t->s[i+1] - t->s[i];
    double f = span > 1e-12 ? (s - t->s[i]) / span : 0.0;
    out->x = pts[i].x + (pts[i+1].x - pts[i].x) * f;
    out->y = pts[i].y + (pts[i+1].y - pts[i].y) * f;
    return 0;
}
