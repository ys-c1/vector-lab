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

int raster_mask_parse(const char *s, unsigned short *out) {
    if (!s || !out) return -1;
    unsigned short m = 0;
    int n = 0;
    for (; s[n]; ++n) {
        if (n >= RMASK_BITS) return -2;            /* 超过 16 位：位数不对 */
        if (s[n] != '0' && s[n] != '1') return -3; /* 非法字符 */
        if (s[n] == '1') m |= (unsigned short)(1u << n);
    }
    if (n != RMASK_BITS) return -2;                /* 不足 16 位：位数不对 */
    *out = m;
    return 0;
}

int raster_mask_count(unsigned short mask) {
    int c = 0;
    for (int k = 0; k < RMASK_BITS; ++k) c += (mask >> k) & 1;
    return c;
}

int raster_fill_mask(const RScene *s, const RBox *box, int w, int h,
                     unsigned short mask, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0) return -1;
    int K = raster_mask_count(mask);
    if (K == 0) return -4;                         /* 全零掩码：无采样点，拒绝渲染 */
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int b = 0; b < RMASK_BITS; ++b) {
                if (!(mask & (1u << b))) continue;
                int col = b & 3, row = b >> 2;
                double px = box->minx + (x + (col + 0.5) / 4.0) * dx;
                double py = box->miny + (y + (row + 0.5) / 4.0) * dy;
                for (int k = 0; k < s->npolys; ++k)
                    if (inside(&s->polys[k], px, py)) { ++hit; break; }
            }
            buf[y * w + x] = (unsigned char)(255 * hit / K); /* 灰度覆盖率 */
        }
    return 0;
}
