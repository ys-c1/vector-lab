#include "pipeline.h"
#include "raster.h"
#include <math.h>
#include <stddef.h>

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

int pipeline_render_ex(const RScene *s, RPlace place, RBox win, int w, int h,
                       const RCullCfg *cfg, RCullStats *st, unsigned char *buf) {
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
    if (!cfg || cfg->mode == RCULL_OFF)
        return raster_fill(&moved, &win, w, h, 4, buf);
    if (cfg->mode == RCULL_HAND)
        return raster_fill_cull(&moved, &win, w, h, 4, cfg->threshold, st, buf);
    if (cfg->mode == RCULL_AUTO) {
        if (cfg->lambda < 0.0) return -5;  /* 负代价权重：拒绝，见 cull.h */
        double sizes[32], covs[32];
        int m = 0;
        for (int k = 0; k < moved.npolys; ++k) {
            double sz = cull_poly_size(&moved.polys[k], &win, w, h);
            if (sz == 0.0) continue;  /* 免费剔除，不参与阈值决策 */
            sizes[m] = sz;
            covs[m] = (double)raster_poly_coverage(&moved.polys[k], &win, w, h, 4);
            ++m;
        }
        double T = 0.0, cost = 0.0;
        if (m > 0 && cull_pick_threshold(sizes, covs, m, cfg->lambda, &T, &cost) != 0)
            return -6;
        int rc = raster_fill_cull(&moved, &win, w, h, 4, T, st, buf);
        if (st) { st->threshold = T; st->expected = cost; }
        return rc;
    }
    return -1;
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    return pipeline_render_ex(s, place, win, w, h, NULL, NULL, buf);
}
