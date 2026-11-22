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

int raster_fill_cull(const RScene *s, const RBox *box, int w, int h, int ss,
                     double threshold, RCullStats *st, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    int keep[32], nk = 0, nfree = 0, nthresh = 0;
    for (int k = 0; k < s->npolys; ++k) {
        double sz = cull_poly_size(&s->polys[k], box, w, h);
        if (sz == 0.0) ++nfree;
        else if (sz < threshold) ++nthresh;
        else keep[nk++] = k;
    }
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
                    for (int k = 0; k < nk; ++k)
                        if (inside(&s->polys[keep[k]], px, py)) { hit = 1; break; }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
    if (st) {
        st->npolys = s->npolys;
        st->free_culls = nfree;
        st->thresh_culls = nthresh;
        st->kept = nk;
        st->threshold = threshold;
        st->expected = 0.0;
    }
    return 0;
}

int raster_poly_coverage(const RPoly *p, const RBox *box, int w, int h, int ss) {
    if (!p || !box || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    int cov = 0;
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss && !hit; ++j)
                for (int i = 0; i < ss && !hit; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    if (inside(p, px, py)) hit = 1;
                }
            if (hit) ++cov;
        }
    return cov;
}
