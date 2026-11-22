#include "pipeline.h"
#include "raster.h"
#include "simplify.h"
#include <math.h>

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    return raster_fill(&moved, &win, w, h, 4, buf);
}

int pipeline_render_simplified(const RScene *s, RPlace place, RBox win,
                                int w, int h, double area_ratio,
                                unsigned char *buf, int *removed_out) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    if (!(area_ratio > 0.0) || area_ratio > 1.0) return -2;
    RScene moved;
    scene_init(&moved);
    int removed_total = 0;
    for (int k = 0; k < s->npolys; ++k) {
        const RPoly *src = &s->polys[k];
        HPoint in[64];
        for (int i = 0; i < src->n; ++i) { in[i].x = src->pts[i].x; in[i].y = src->pts[i].y; }
        HSimp sr;
        int rc = hull_simplify(in, src->n, area_ratio, &sr);
        if (rc == -2) return -2;
        if (rc != 0) return -5;
        removed_total += src->n - sr.kept;
        RPoly tmp;
        tmp.n = sr.kept;
        for (int i = 0; i < sr.kept; ++i) {
            RPoint q = {sr.poly.pts[i].x, sr.poly.pts[i].y};
            tmp.pts[i] = move(q, place);
        }
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    if (removed_out) *removed_out = removed_total;
    return raster_fill(&moved, &win, w, h, 4, buf);
}
