#include "pipeline.h"
#include "raster.h"
#include <math.h>

/* ---- 基线路径：表达式与顺序保持原样，逐字节兼容既有产物。 ---- */
static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

/* ---- 确定性路径 ----
 * 1) 禁用乘加合并：确定性构建以 -ffp-contract=off 编译（见 Makefile DET_CFLAGS），
 *    a*b+c 绝不会被融合成单条 FMA；下列每次乘法/加法都是独立、显式的运算；
 * 2) 固定求值顺序：每一步拆成具名临时量，严格按书写顺序串行，
 *    不依赖编译器对同级运算的结合/重排（确定性构建也不含 -ffast-math）。
 */
static RPoint move_fixed(RPoint p, RPlace q) {
    double c = cos(q.rot);
    double s = sin(q.rot);
    double ax;   /* p.x * c */
    double by;   /* p.y * s */
    double rotx; /* ax - by */
    double scx;  /* rotx * scale */
    double rx;   /* scx + tx */
    double ay;   /* p.x * s */
    double bx;   /* p.y * c */
    double roty; /* ay + bx */
    double scy;  /* roty * scale */
    double ry;   /* scy + ty */

    ax = p.x * c;
    by = p.y * s;
    rotx = ax - by;
    scx = rotx * q.scale;
    rx = scx + q.tx;

    ay = p.x * s;
    bx = p.y * c;
    roty = ay + bx;
    scy = roty * q.scale;
    ry = scy + q.ty;

    RPoint r;
    r.x = rx;
    r.y = ry;
    return r;
}

static int render_common(const RScene *s, RPlace place, RBox win, int w, int h,
                         int fixed, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    scene_init(&moved);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i)
            tmp.pts[i] = fixed ? move_fixed(tmp.pts[i], place)
                               : move(tmp.pts[i], place);
        if (scene_add_poly(&moved, tmp.pts, tmp.n) != 0) return -4;
    }
    return raster_fill_cfg(&moved, &win, w, h, 4, fixed, buf);
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    return render_common(s, place, win, w, h, 0, buf);
}

int pipeline_render_cfg(const RScene *s, RPlace place, RBox win, int w, int h,
                        const RFPConf *cfg, unsigned char *buf) {
    if (!cfg) return -5;
    return render_common(s, place, win, w, h, cfg->fp_deterministic, buf);
}
