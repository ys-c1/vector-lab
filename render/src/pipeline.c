#define _POSIX_C_SOURCE 199309L /* clock_gettime，-std=c11 下需要显式声明 */
#include "pipeline.h"
#include "raster.h"
#include "zoom.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

static RPoint move(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

static int move_scene(const RScene *s, RPlace place, double scale, RScene *out) {
    scene_init(out);
    for (int k = 0; k < s->npolys; ++k) {
        RPoly tmp = s->polys[k];
        for (int i = 0; i < tmp.n; ++i) {
            RPlace q = place;
            q.scale = scale;
            tmp.pts[i] = move(tmp.pts[i], q);
        }
        if (scene_add_poly(out, tmp.pts, tmp.n) != 0) return -1;
    }
    return 0;
}

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (place.scale == 0.0) return -2;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    if (move_scene(s, place, place.scale, &moved) != 0) return -4;
    return raster_fill(&moved, &win, w, h, 4, buf);
}

static unsigned long now_us(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return 0;
    return (unsigned long)ts.tv_sec * 1000000UL + (unsigned long)(ts.tv_nsec / 1000);
}

static int cover_at(const RScene *s, RPlace place, double scale, RBox win,
                    int w, int h, int ss, unsigned char *buf) {
    RScene moved;
    if (move_scene(s, place, scale, &moved) != 0) return -4;
    return raster_cover(&moved, &win, w, h, ss, buf);
}

int pipeline_render_zoom(const RScene *s, RPlace place, RBox win, int w, int h,
                         const RZoomOpts *opt, unsigned char *buf, RZoomReport *rep) {
    if (!s || !buf || w <= 0 || h <= 0) return -1;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;

    /* 先判 scale：零和负数是硬性拒绝，先于任何取对数。 */
    if (!isfinite(place.scale)) return -7;
    if (!(place.scale > 0.0)) return -2;

    int want = opt ? opt->level : -1;
    RZoomLevel zl;
    if (want >= 0) {
        if (zoom_level_split((double)want, &zl) != 0) return -5;
    } else {
        int rc = zoom_level_from_scale(place.scale, &zl);
        if (rc == -1) return -5; /* 正数但层号超出可表示范围 */
        if (rc != 0) return -2;
    }
    int ss = (opt && opt->ss > 0) ? opt->ss : 4;
    if (ss > 8) ss = 8;
    int blend = opt ? opt->blend : 1;

    double scale_lo = zoom_scale_of_level(zl.lo);
    double scale_hi = zoom_scale_of_level(zl.hi);
    unsigned long t0 = now_us();
    int rc;
    if (!blend) {
        /* 硬切：只取最近的一档，二值填充；整数层上就是旧路径。 */
        int near_hi = !zl.exact && zl.w > 0.5;
        double sc = near_hi ? scale_hi : scale_lo;
        RScene moved;
        if (move_scene(s, place, sc, &moved) != 0) rc = -4;
        else rc = raster_fill(&moved, &win, w, h, ss, buf);
    } else if (zl.exact) {
        /* 恰在整数层：只渲染一档覆盖率，权重为 0，不做插值。 */
        rc = cover_at(s, place, scale_lo, win, w, h, ss, buf);
    } else {
        /* 两档各自使用自己的整数层缩放，按权重 w 逐像素插值覆盖率。 */
        size_t n = (size_t)w * (size_t)h;
        unsigned char *lo = (unsigned char*)malloc(n);
        unsigned char *hi = (unsigned char*)malloc(n);
        if (!lo || !hi) { free(lo); free(hi); return -6; }
        rc = cover_at(s, place, scale_lo, win, w, h, ss, lo);
        if (rc == 0) rc = cover_at(s, place, scale_hi, win, w, h, ss, hi);
        if (rc == 0) rc = raster_blend(lo, hi, zl.w, w * h, buf);
        free(lo);
        free(hi);
    }
    unsigned long t1 = now_us();

    if (rep) {
        rep->level = zl.level;
        rep->lo = zl.lo;
        rep->hi = zl.hi;
        rep->w = zl.w;
        rep->exact = zl.exact;
        rep->scale_lo = scale_lo;
        rep->scale_hi = scale_hi;
        rep->scale_eff = zl.exact ? scale_lo : zoom_scale_of_level_frac(zl.level);
        rep->us = t1 - t0;
    }
    return rc;
}
