#include "bbox.h"

int bbox_of_points(const GPoint *pts, int n, GBox *out) {
    if (!pts || !out || n <= 0) return -1;
    out->minx = out->maxx = pts[0].x;
    out->miny = out->maxy = pts[0].y;
    for (int i = 1; i < n; ++i) {
        if (pts[i].x < out->minx) out->minx = pts[i].x;
        if (pts[i].x > out->maxx) out->maxx = pts[i].x;
        if (pts[i].y < out->miny) out->miny = pts[i].y;
        if (pts[i].y > out->maxy) out->maxy = pts[i].y;
    }
    return 0;
}

int bbox_union(const GBox *a, const GBox *b, GBox *out) {
    if (!a || !b || !out) return -1;
    out->minx = a->minx < b->minx ? a->minx : b->minx;
    out->miny = a->miny < b->miny ? a->miny : b->miny;
    out->maxx = a->maxx > b->maxx ? a->maxx : b->maxx;
    out->maxy = a->maxy > b->maxy ? a->maxy : b->maxy;
    return 0;
}

double bbox_area(const GBox *b) {
    if (!b) return -1.0;
    return (b->maxx - b->minx) * (b->maxy - b->miny);
}
