#include "scene.h"

void scene_init(RScene *s) { if (s) s->npolys = 0; }

int scene_add_poly(RScene *s, const RPoint *pts, int n) {
    if (!s || !pts || n < 3 || n > 64) return -1;
    if (s->npolys >= 32) return -2;
    RPoly *p = &s->polys[s->npolys];
    p->n = n;
    for (int i = 0; i < n; ++i) p->pts[i] = pts[i];
    s->npolys++;
    return 0;
}

int scene_bbox(const RScene *s, RBox *out) {
    if (!s || !out || s->npolys == 0) return -1;
    out->minx = out->maxx = s->polys[0].pts[0].x;
    out->miny = out->maxy = s->polys[0].pts[0].y;
    for (int k = 0; k < s->npolys; ++k)
        for (int i = 0; i < s->polys[k].n; ++i) {
            double x = s->polys[k].pts[i].x, y = s->polys[k].pts[i].y;
            if (x < out->minx) out->minx = x;
            if (x > out->maxx) out->maxx = x;
            if (y < out->miny) out->miny = y;
            if (y > out->maxy) out->maxy = y;
        }
    return 0;
}

int scene_count_verts(const RScene *s) {
    if (!s) return -1;
    int n = 0;
    for (int k = 0; k < s->npolys; ++k) n += s->polys[k].n;
    return n;
}
