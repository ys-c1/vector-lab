#include "raster.h"
#include <string.h>

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

static void fill_box(const RScene *s, const RBox *box, int w, int h, int ss,
                     int x0, int y0, int x1, int y1, unsigned char *buf) {
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    for (int k = 0; k < s->npolys; ++k)
                        if (inside(&s->polys[k], px, py)) { hit = 1; break; }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
}

int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    memset(buf, 0, (size_t)w * (size_t)h);
    fill_box(s, box, w, h, ss, 0, 0, w, h, buf);
    return 0;
}

int raster_fill_region(const RScene *s, const RBox *box, int w, int h, int ss,
                       int x0, int y0, int rw, int rh, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (x0 < 0 || y0 < 0 || rw <= 0 || rh <= 0) return -1;
    if (x0 > w - rw || y0 > h - rh) return -1;
    fill_box(s, box, w, h, ss, x0, y0, x0 + rw, y0 + rh, buf);
    return 0;
}
