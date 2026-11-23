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

int raster_fill_rows(const RScene *s, const RBox *box, int w, int h, int ss,
                     int y0, int y1, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (y0 < 0 || y0 > y1 || y1 > h) return -2;
    /* 每个像素的采样点只由 x、y 与 box 决定，因此按行拆分不会改变任何像素值。 */
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = y0; y < y1; ++y)
        for (int x = 0; x < w; ++x) {
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
    return 0;
}

int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    memset(buf, 0, (size_t)w * (size_t)h);
    return raster_fill_rows(s, box, w, h, ss, 0, h, buf);
}
