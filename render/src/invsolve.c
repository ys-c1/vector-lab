#define _POSIX_C_SOURCE 199309L
#include "invsolve.h"
#include "raster.h"
#include <math.h>
#include <string.h>
#include <time.h>

#define SS 4          /* 与 pipeline_render 固定超采样一致，保证像素逐字节可比 */
#define BISECT_ITERS 40
#define AREA_EPS 1e-12

/* 与 raster.c 相同的射线法判定，保证扩大缓冲与窗口缓冲采样结论一致。 */
static int inside(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static double poly_area(const RPoly *p) {
    double a = 0.0;
    for (int i = 0; i < p->n; ++i) {
        RPoint q = p->pts[i], r = p->pts[(i + 1) % p->n];
        a += q.x * r.y - r.x * q.y;
    }
    return fabs(a) * 0.5;
}

static void scene_bbox2(const RScene *s, RBox *b) {
    b->minx = b->maxx = s->polys[0].pts[0].x;
    b->miny = b->maxy = s->polys[0].pts[0].y;
    for (int k = 0; k < s->npolys; ++k)
        for (int i = 0; i < s->polys[k].n; ++i) {
            double x = s->polys[k].pts[i].x, y = s->polys[k].pts[i].y;
            if (x < b->minx) b->minx = x;
            if (x > b->maxx) b->maxx = x;
            if (y < b->miny) b->miny = y;
            if (y > b->maxy) b->maxy = y;
        }
}

static void scene_scaled(const RScene *s, double sc, RScene *t) {
    t->npolys = s->npolys;
    for (int k = 0; k < s->npolys; ++k) {
        t->polys[k].n = s->polys[k].n;
        for (int i = 0; i < s->polys[k].n; ++i) {
            t->polys[k].pts[i].x = s->polys[k].pts[i].x * sc;
            t->polys[k].pts[i].y = s->polys[k].pts[i].y * sc;
        }
    }
}

static long count_on(const unsigned char *buf, int n) {
    long c = 0;
    for (int i = 0; i < n; ++i) if (buf[i]) ++c;
    return c;
}

/* 计算缩放上界：缩放后包围盒每边至少让出一个采样格，保证不触碰窗口边界。 */
static double max_scale(const RBox *b, const RBox *win, int w, int h) {
    double dx = (win->maxx - win->minx) / w;
    double dy = (win->maxy - win->miny) / h;
    double smax = 1e300;
    double lim;
    if (b->minx < 0.0 && (lim = (win->minx + dx) / b->minx) < smax) smax = lim;
    if (b->maxx > 0.0 && (lim = (win->maxx - dx) / b->maxx) < smax) smax = lim;
    if (b->miny < 0.0 && (lim = (win->miny + dy) / b->miny) < smax) smax = lim;
    if (b->maxy > 0.0 && (lim = (win->maxy - dy) / b->maxy) < smax) smax = lim;
    return smax;
}

/* 边缘验证：在与窗口同格距、同采样相位的扩大网格上栅格化完整场景，
 * 统计落在窗口外的命中像素数。返回 -1 表示扩大缓冲容量不足。 */
long inverse_edge_lost(const RScene *t, const RBox *win, int w, int h,
                       unsigned char *fullbuf, int cap) {
    RBox bb;
    scene_bbox2(t, &bb);
    double dx = (win->maxx - win->minx) / w;
    double dy = (win->maxy - win->miny) / h;
    int x0 = (int)floor((bb.minx - win->minx) / dx);
    int x1 = (int)ceil((bb.maxx - win->minx) / dx);
    int y0 = (int)floor((bb.miny - win->miny) / dy);
    int y1 = (int)ceil((bb.maxy - win->miny) / dy);
    int wb = x1 - x0, hb = y1 - y0;
    if (wb <= 0 || hb <= 0 || (long)wb * hb > cap) return -1;
    memset(fullbuf, 0, (size_t)wb * (size_t)hb);
    for (int by = 0; by < hb; ++by)
        for (int bx = 0; bx < wb; ++bx) {
            int hit = 0;
            for (int j = 0; j < SS && !hit; ++j)
                for (int i = 0; i < SS && !hit; ++i) {
                    double px = win->minx + (x0 + bx + (i + 0.5) / SS) * dx;
                    double py = win->miny + (y0 + by + (j + 0.5) / SS) * dy;
                    for (int k = 0; k < t->npolys; ++k)
                        if (inside(&t->polys[k], px, py)) { hit = 1; break; }
                }
            fullbuf[(size_t)by * wb + bx] = hit ? 255 : 0;
        }
    /* 与窗口同网格的格号落在窗口外，即被窗口裁掉的边缘像素。 */
    long lost = 0;
    for (int by = 0; by < hb; ++by)
        for (int bx = 0; bx < wb; ++bx) {
            if (!fullbuf[(size_t)by * wb + bx]) continue;
            int cx = x0 + bx, cy = y0 + by;
            if (cx < 0 || cx >= w || cy < 0 || cy >= h) ++lost;
        }
    return lost;
}

int inverse_solve(const RScene *s, const RBox *win, int w, int h,
                  double target_density, double scale_cap,
                  InvResult *out, unsigned char *buf, unsigned char *fullbuf) {
    if (!s || !win || !out || !buf || !fullbuf || s->npolys <= 0) return -1;
    if (w <= 0 || h <= 0 || !(target_density > 0.0)) return -1;

    memset(out, 0, sizeof(*out));
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    double area = 0.0;
    for (int k = 0; k < s->npolys; ++k) {
        double a = poly_area(&s->polys[k]);
        area += a;
        if (a <= AREA_EPS) { ++out->degenerate_polys; out->degenerate = 1; }
    }
    out->area = area;
    if (area <= AREA_EPS) return -7;   /* 总面积为零：密度无定义，拒绝并由调用方报告 */

    if (!(win->maxx > win->minx) || !(win->maxy > win->miny)) return -1;
    RBox base;
    scene_bbox2(s, &base);
    double smax = max_scale(&base, win, w, h);
    if (scale_cap > 0.0 && scale_cap < smax) smax = scale_cap;
    if (!(smax > 0.0) || !isfinite(smax)) return -8;
    out->scale_max = smax;

    /* 上界可达性检查：上界处密度仍不足，则目标越界，拒绝并给出上限。 */
    RScene t;
    scene_scaled(s, smax, &t);
    if (raster_fill(&t, win, w, h, SS, buf) != 0) return -1;
    long max_on = count_on(buf, w * h);
    double max_density = (double)max_on / area;
    if (max_density < target_density) return -8;

    /* 二分最小缩放：命中数关于缩放单调，0 处必然不达标。 */
    double lo = 0.0, hi = smax;
    int iters = 0;
    for (iters = 0; iters < BISECT_ITERS; ++iters) {
        double mid = (lo + hi) * 0.5;
        scene_scaled(s, mid, &t);
        raster_fill(&t, win, w, h, SS, buf);
        double d = (double)count_on(buf, w * h) / area;
        if (d >= target_density) hi = mid; else lo = mid;
    }
    out->iters = iters;
    out->scale = hi;

    /* 在反解缩放下出最终图，并做边缘验证。 */
    scene_scaled(s, hi, &t);
    if (raster_fill(&t, win, w, h, SS, buf) != 0) return -1;
    out->on_pixels = count_on(buf, w * h);
    out->density = (double)out->on_pixels / area;
    long lost = inverse_edge_lost(&t, win, w, h, fullbuf, (w + 8) * (h + 8));
    if (lost < 0) return -1;
    out->edge_lost = lost;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    out->elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                      (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
    return 0;
}
