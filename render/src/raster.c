#include "raster.h"
#include "jitter.h"
#include <stdlib.h>
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
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
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

int raster_fill_jitter(const RScene *s, const RBox *box, int w, int h, int ss,
                       uint64_t seed, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (seed == 0) return -5;
    size_t per = (size_t)ss * (size_t)ss;
    size_t total = per * (size_t)w * (size_t)h;
    if (total / per != (size_t)w * (size_t)h || total > (size_t)0x7fffffff) return -1;
    RJitter *seq = (RJitter*)malloc(total * sizeof(*seq));
    if (!seq) return -6;
    if (jitter_gen(seed, (int)total, seq) != 0) { free(seq); return -5; }
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hits = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    /* Fixed per-pixel order, identical to the exported
                     * sequence ordering: t = y*ss + x, ux then uy. */
                    RJitter jt = seq[((size_t)y * (size_t)w + x) * per +
                                    (size_t)j * (size_t)ss + i];
                    double px = box->minx + (x + ((double)i + jt.ux) / ss) * dx;
                    double py = box->miny + (y + ((double)j + jt.uy) / ss) * dy;
                    for (int k = 0; k < s->npolys; ++k)
                        if (inside(&s->polys[k], px, py)) { ++hits; break; }
                }
            buf[y * w + x] = (unsigned char)(255 * hits / (ss * ss));
        }
    free(seq);
    return 0;
}
