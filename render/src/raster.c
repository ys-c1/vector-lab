#include "raster.h"
#include "guard.h"
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

int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    /* Pre-fill before any computation so a code path that fails to write a
     * pixel exposes the sentinel instead of stale heap contents.  With the
     * guard disabled this is the original zero fill. */
    rguard_buffer(buf, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    int skip = rguard_injected_skip_pixel();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            if (skip >= 0 && y * w + x == skip) continue; /* injected fault */
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
