#include "pipeline.h"
#include "raster.h"
#include "gamma.h"
#include <math.h>

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    RExportGamma off = {0, 1.0};
    return pipeline_render_gamma(s, place, win, w, h, buf, &off);
}

int pipeline_render_gamma(const RScene *s, RPlace place, RBox win, int w, int h,
                          unsigned char *buf, const RExportGamma *opt) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    double gamma = 1.0;
    if (opt && opt->enabled) {
        if (gamma_validate(opt->gamma, NULL, 0) != GAM_OK) return -10;
        gamma = opt->gamma;
    }
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    int rc = raster_fill(&moved, &win, w, h, 4, buf);
    if (rc != 0) return rc;
    /* 关闭新逻辑时 gamma 恒为 1，gamma_apply 是逐级恒等查表，输出不变。 */
    if (gamma != 1.0)
        return gamma_apply(buf, w, h, gamma, buf, NULL, 0);
    return 0;
}
