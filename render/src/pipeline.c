#include "pipeline.h"
#include "raster.h"
#include "fit.h"
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

int pipeline_render_fit(const RScene *s, RPlace place, int w, int h, int opt_rot,
                        unsigned char *buf, double *angle) {
    if (!s || !buf || w <= 0 || h <= 0 || s->npolys == 0) return -1;
    if (place.scale == 0.0) return -2;

    double ang = 0.0;
    if (opt_rot) {
        /* 场景上限 32 多边形 x 64 顶点，栈上收集即可。 */
        FPoint pts[32 * 64];
        int nv = 0;
        for (int k = 0; k < s->npolys; ++k)
            for (int i = 0; i < s->polys[k].n; ++i) {
                pts[nv].x = s->polys[k].pts[i].x;
                pts[nv].y = s->polys[k].pts[i].y;
                ++nv;
            }
        FitResult fr;
        if (fit_optimize(pts, nv, &fr) != 0) return -4;
        if (fit_validate(fr.angle) != 0) return -5;   /* 角度越界拒绝：合法域 [0, pi/2) */
        ang = fr.angle;
    }
    if (angle) *angle = ang;

    /* 先按 -ang 预旋转几何（fit 给出的角是“转回轴对齐”所需旋转），再走原 place 流程。 */
    double pc = cos(-ang), ps = sin(-ang);
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) {
            RPoint p = tmp.pts[i];
            RPoint q;
            q.x = p.x * pc - p.y * ps;
            q.y = p.x * ps + p.y * pc;
            tmp.pts[i] = move(q, place);
        }
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    RBox win;
    if (scene_bbox(&moved, &win) != 0) return -4;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    return raster_fill(&moved, &win, w, h, 4, buf);
}
