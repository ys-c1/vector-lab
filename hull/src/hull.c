#include "hull.h"
#include <math.h>
#include <stdlib.h>

static int cmp(const void *a, const void *b) {
    const HPoint *p = (const HPoint*)a, *q = (const HPoint*)b;
    if (p->x < q->x) return -1;
    if (p->x > q->x) return 1;
    if (p->y < q->y) return -1;
    if (p->y > q->y) return 1;
    return 0;
}

static double cross(HPoint o, HPoint a, HPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

int hull_convex(const HPoint *pts, int n, HPoly *out) {
    if (!pts || !out || n < 1) return -1;
    HPoint *s = (HPoint*)malloc(sizeof(HPoint) * (size_t)n);
    if (!s) return -1;
    for (int i = 0; i < n; ++i) s[i] = pts[i];
    qsort(s, (size_t)n, sizeof(HPoint), cmp);
    if (n == 1) { out->pts[0] = s[0]; out->count = 1; free(s); return 0; }
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(out->pts[k-2], out->pts[k-1], s[i]) <= 0.0) --k;
        out->pts[k++] = s[i];
    }
    int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && cross(out->pts[k-2], out->pts[k-1], s[i]) <= 0.0) --k;
        out->pts[k++] = s[i];
    }
    out->count = k - 1;
    free(s);
    return 0;
}

int hull_min_rect(const HPoint *pts, int n, double *cx, double *cy, double *w, double *h, double *angle) {
    if (!pts || !cx || !cy || !w || !h || !angle || n < 1) return -1;
    double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; ++i) {
        if (pts[i].x < minx) minx = pts[i].x;
        if (pts[i].x > maxx) maxx = pts[i].x;
        if (pts[i].y < miny) miny = pts[i].y;
        if (pts[i].y > maxy) maxy = pts[i].y;
    }
    *w = maxx - minx; *h = maxy - miny;
    *cx = (minx + maxx) * 0.5; *cy = (miny + maxy) * 0.5;
    *angle = 0.0;
    return 0;
}
