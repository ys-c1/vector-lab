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

int pipeline_render_fit(const RScene *s, RPlace place, RBox win, int w, int h,
                        unsigned char *buf, RMargin *used) {
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
    RBox bb;
    if (scene_bbox(&moved, &bb) != 0) return RMARGIN_EMPTY;
    RMargin m;
    int rc = margin_solve(&bb, &win, w, h, &m);
    if (rc != RMARGIN_OK && rc != RMARGIN_REJECT) return -3;
    if (used) *used = m;
    if (rc == RMARGIN_REJECT) return RMARGIN_REJECT;
    RBox ex = margin_expand(&win, w, h, m);
    return raster_fill(&moved, &ex, w, h, 4, buf);
}
