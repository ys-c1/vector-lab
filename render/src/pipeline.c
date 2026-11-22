#include "pipeline.h"
#include "raster.h"
#include "adapt.h"
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

/* 老路径与自适应路径共用的唯一渲染入口，保证关掉新逻辑时逐字节不变。 */
static int render_core(const RScene *s, RPlace place, const RBox *win,
                       int w, int h, int ss, unsigned char *buf) {
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    return raster_fill(&moved, win, w, h, ss, buf);
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    return render_core(s, place, &win, w, h, 4, buf);
}

/* 自适应探针上下文：场景/窗口/尺寸固定，探针只改 ss。 */
typedef struct {
    const RScene *s;
    RPlace place;
    const RBox *win;
    int w, h;
} PipeCtx;

static int pipe_probe(int ss, void *ctx, unsigned char *buf) {
    const PipeCtx *c = (const PipeCtx*)ctx;
    return render_core(c->s, c->place, c->win, c->w, c->h, ss, buf);
}

int pipeline_render_opt(const RScene *s, RPlace place, RBox win, int w, int h,
                        int adaptive, double tol, unsigned char *buf,
                        AdaptResult *info) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    if (!adaptive) {
        /* 新逻辑关闭：与 pipeline_render 完全同参同函数。 */
        int rc = render_core(s, place, &win, w, h, 4, buf);
        if (info) memset(info, 0, sizeof(*info));
        return rc;
    }
    if (!(tol > 0.0) || tol > 1.0) return -5;

    /* 参照需要变换后的场景，render_core 内部每次都会重算；
       这里显式构建一次 moved 供覆盖率参照使用。 */
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) tmp.pts[i] = move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    int *cov = NULL;
    if (w > 0 && h > 0)
        cov = (int*)malloc((size_t)w * (size_t)h * sizeof(int));
    if (!cov) return -6;
    AdaptRef ref = {&moved, &win, w, h, cov};
    int rc = adapt_ref_build(&ref);
    if (rc != ADAPT_OK) { free(cov); return rc; }

    PipeCtx pc = {s, place, &win, w, h};
    AdaptTarget t = {
        pipe_probe, &pc, &ref, tol,
        ADAPT_SS_MIN, ADAPT_SS_MAX, buf
    };
    rc = adapt_solve(&t, info);
    free(cov);
    if (rc == ADAPT_ERR_NOPASS || rc == ADAPT_ERR_NONMONO) return rc;
    if (rc != ADAPT_OK) return rc;
    /* adapt_solve 成功时最后一次探针已把达标结果写入 buf。 */
    return 0;
}
