#include "pipeline.h"
#include "raster.h"
#include <math.h>

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

static int place_scene(const RScene *s, RPlace q, RScene *moved) {
    scene_init(moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], q);
        if (scene_add_poly(moved, tmp.pts, tmp.n) != 0) return -4;
    }
    return 0;
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    int rc = place_scene(s, place, &moved);
    if (rc != 0) return rc;
    return raster_fill(&moved, &win, w, h, 4, buf);
}

int pipeline_render_mask(const RScene *s, RPlace place, RBox win, int w, int h,
                         unsigned short mask, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    int rc = place_scene(s, place, &moved);
    if (rc != 0) return rc;
    return raster_fill_mask(&moved, &win, w, h, mask, buf); /* 全零掩码返回 -4 */
}
