#include "arrow.h"
#include <math.h>
#include <string.h>

/* 与 pipeline.c 一致的放置变换（保持像素级对齐） */
static RPoint amove(RPoint p, RPlace q) {
    double c = cos(q.rot), s = sin(q.rot);
    RPoint r;
    r.x = (p.x * c - p.y * s) * q.scale + q.tx;
    r.y = (p.x * s + p.y * c) * q.scale + q.ty;
    return r;
}

int arrow_layout(const RPoint *pts, int n, double density, RArrowSet *out) {
    if (!pts || !out || n < 2) return ARROW_EARGS;
    if (!(density > 0.0)) return ARROW_EDENSITY;
    double s[65];
    s[0] = 0.0;
    for (int i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i-1].x, dy = pts[i].y - pts[i-1].y;
        s[i] = s[i-1] + sqrt(dx * dx + dy * dy);
    }
    double total = s[n-1];
    if (!(total > 0.0)) return ARROW_ELENGTH;
    double spacing = 1.0 / density;
    int count = (int)floor(total / spacing + 1e-9);
    if (count > ARROW_MAX) return ARROW_EFULL;
    out->n = count;
    for (int k = 0; k < count; ++k) {
        double target = (k + 0.5) * spacing;
        int i = 0;
        while (i + 2 < n && s[i+1] < target) ++i;
        double span = s[i+1] - s[i];
        double f = span > 1e-12 ? (target - s[i]) / span : 0.0;
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        out->a[k].x = pts[i].x + dx * f;
        out->a[k].y = pts[i].y + dy * f;
        out->a[k].angle = atan2(dy, dx);
    }
    return ARROW_OK;
}

/* 在已渲染的缓冲上叠加一个实心三角形（不清屏，只置亮），
 * 采样规则与 raster_fill 相同（ss=4），保证风格一致。 */
static void draw_tri(const RPoint *t, const RBox *box, int w, int h, unsigned char *buf) {
    const int ss = 4;
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    double minx = t[0].x, maxx = t[0].x, miny = t[0].y, maxy = t[0].y;
    for (int i = 1; i < 3; ++i) {
        if (t[i].x < minx) minx = t[i].x;
        if (t[i].x > maxx) maxx = t[i].x;
        if (t[i].y < miny) miny = t[i].y;
        if (t[i].y > maxy) maxy = t[i].y;
    }
    int x0 = (int)floor((minx - box->minx) / dx), x1 = (int)ceil((maxx - box->minx) / dx);
    int y0 = (int)floor((miny - box->miny) / dy), y1 = (int)ceil((maxy - box->miny) / dy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    double d = (t[1].x - t[0].x) * (t[2].y - t[0].y) - (t[1].y - t[0].y) * (t[2].x - t[0].x);
    if (d == 0.0) return;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            int hit = 0;
            for (int j = 0; j < ss && !hit; ++j)
                for (int i = 0; i < ss && !hit; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    double w0 = (t[1].x - t[0].x) * (py - t[0].y) - (t[1].y - t[0].y) * (px - t[0].x);
                    double w1 = (t[2].x - t[1].x) * (py - t[1].y) - (t[2].y - t[1].y) * (px - t[1].x);
                    double w2 = (t[0].x - t[2].x) * (py - t[2].y) - (t[0].y - t[2].y) * (px - t[2].x);
                    if ((w0 >= 0 && w1 >= 0 && w2 >= 0) || (w0 <= 0 && w1 <= 0 && w2 <= 0))
                        hit = 1;
                }
            if (hit) buf[y * w + x] = 255;
        }
}

int pipeline_render_arrows(const RScene *s, RPlace place, RBox win, int w, int h,
                           const RPoint *path, int npath, double density,
                           double arrow_len, unsigned char *buf) {
    if (!path || npath < 2)  /* 箭头关闭：与当前构建逐字节一致 */
        return pipeline_render(s, place, win, w, h, buf);
    if (!s || !buf || w <= 0 || h <= 0) return ARROW_EARGS;
    RArrowSet set;
    int rc = arrow_layout(path, npath, density, &set);
    if (rc != ARROW_OK) return rc;
    rc = pipeline_render(s, place, win, w, h, buf);
    if (rc != 0) return rc;
    if (!(arrow_len > 0.0)) return ARROW_EARGS;
    for (int k = 0; k < set.n; ++k) {
        double c = cos(set.a[k].angle), sn = sin(set.a[k].angle);
        RPoint p = { set.a[k].x, set.a[k].y };
        RPoint tip  = { p.x + c * arrow_len * 0.6,            p.y + sn * arrow_len * 0.6 };
        RPoint bas  = { p.x - c * arrow_len * 0.4,            p.y - sn * arrow_len * 0.4 };
        RPoint tri[3];
        tri[0] = tip;
        tri[1].x = bas.x - sn * arrow_len * 0.35; tri[1].y = bas.y + c * arrow_len * 0.35;
        tri[2].x = bas.x + sn * arrow_len * 0.35; tri[2].y = bas.y - c * arrow_len * 0.35;
        RPoint m[3];
        for (int i = 0; i < 3; ++i) m[i] = amove(tri[i], place);
        draw_tri(m, &win, w, h, buf);
    }
    return ARROW_OK;
}
