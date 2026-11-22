#include "winsolve.h"
#include <stdio.h>
#include <string.h>

/* 奇偶规则：逐表达式复刻 render/src/raster.c 的 inside()。 */
static int inside_ring(const WPoint *p, int n, double x, double y) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        WPoint a = p[i], b = p[(i + 1) % n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

/* 全部归一化子环并成一个边集合后的奇偶覆盖：与子环方向无关。 */
static int even_odd_rings(const WNorm *nrm, double x, double y) {
    int c = 0;
    for (int k = 0; k < nrm->rings; ++k) {
        const WRing *r = &nrm->ring[k];
        for (int i = 0; i < r->n; ++i) {
            WPoint a = r->pts[i], b = r->pts[(i + 1) % r->n];
            if ((a.y > y) == (b.y > y)) continue;
            double t = (y - a.y) / (b.y - a.y);
            if (a.x + (b.x - a.x) * t > x) ++c;
        }
    }
    return c & 1;
}

/* 面积多重集比较：排序后逐项比较（调用方保证 n <= WN_MAX_RINGS）。 */
static int same_multiset(const double *a, int na, const double *b, int nb) {
    if (na != nb) return 0;
    double x[WN_MAX_RINGS], y[WN_MAX_RINGS];
    memcpy(x, a, sizeof(double) * (size_t)na);
    memcpy(y, b, sizeof(double) * (size_t)nb);
    for (int i = 0; i < na; ++i)
        for (int j = i + 1; j < na; ++j) {
            if (x[j] < x[i]) { double t = x[i]; x[i] = x[j]; x[j] = t; }
            if (y[j] < y[i]) { double t = y[i]; y[i] = y[j]; y[j] = t; }
        }
    for (int i = 0; i < na; ++i)
        if (x[i] != y[i]) return 0;
    return 1;
}

void ws_build(const WFileSet *src, WSet *dst) {
    if (!src || !dst) return;
    dst->count = 0;
    for (int k = 0; k < src->count && k < WS_MAX_WINDOWS; ++k) {
        const WSWindow *s = &src->win[k];
        WItem *d = &dst->win[dst->count];
        memset(d, 0, sizeof *d);
        d->id = s->id;
        d->in_n = s->in_n;
        memcpy(d->in, s->in, sizeof(WPoint) * (size_t)s->in_n);
        d->nsplit = s->nsplit;
        for (int i = 0; i < s->nsplit; ++i) {
            d->split_a[i] = s->split_a[i];
            d->split_b[i] = s->split_b[i];
        }
        d->declared_rings = s->nsplit > 0 ? s->rings : 0;

        d->rc = winnorm_normalize(s->in, s->in_n, s->nsplit > 0 ? 1 : 0,
                                  &d->nrm, &d->at, &d->at2);
        if (d->rc == WN_OK) {
            d->rings = d->nrm.rings;
            d->total_abs = 0.0;
            double modabs[WN_MAX_RINGS];
            for (int r = 0; r < d->nrm.rings; ++r) {
                modabs[r] = d->nrm.abs_area[r];
                d->total_abs += d->nrm.abs_area[r];
            }
            if (s->nsplit > 0) {
                d->split_ok = same_multiset(modabs, d->nrm.rings,
                                            s->raw_abs, s->rings);
                d->rings = d->nrm.rings;
            } else {
                d->split_ok = 1;
            }
        } else {
            d->rings = 0;
            d->split_ok = 0;
            d->total_abs = 0.0;
        }
        ++dst->count;
    }
}

int ws_bbox(const WSet *s, WBox *out) {
    if (!s || !out || s->count <= 0) return -1;
    int first = 1;
    for (int k = 0; k < s->count; ++k) {
        for (int i = 0; i < s->win[k].in_n; ++i) {
            double x = s->win[k].in[i].x, y = s->win[k].in[i].y;
            if (first) {
                out->minx = out->maxx = x;
                out->miny = out->maxy = y;
                first = 0;
            } else {
                if (x < out->minx) out->minx = x;
                if (x > out->maxx) out->maxx = x;
                if (y < out->miny) out->miny = y;
                if (y > out->maxy) out->maxy = y;
            }
        }
    }
    return first ? -1 : 0;
}

int ws_render(const WSet *s, WBox win, int w, int h, int ss, int mode,
              unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -2;
    if (mode != 0 && mode != 1) return -3;
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (win.maxx - win.minx) / w;
    double dy = (win.maxy - win.miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss && !hit; ++j)
                for (int i = 0; i < ss && !hit; ++i) {
                    double px = win.minx + (x + (i + 0.5) / ss) * dx;
                    double py = win.miny + (y + (j + 0.5) / ss) * dy;
                    if (mode == 0) {
                        for (int k = 0; k < s->count && !hit; ++k)
                            if (inside_ring(s->win[k].in, s->win[k].in_n, px, py))
                                hit = 1;
                    } else {
                        /* 新路径：全部子环并集按奇偶覆盖。方向无关，所以
                         * 与老路径逐像素相同是严格结论，不是巧合。 */
                        for (int k = 0; k < s->count && !hit; ++k) {
                            if (s->win[k].rc != WN_OK) continue;
                            if (even_odd_rings(&s->win[k].nrm, px, py)) hit = 1;
                        }
                    }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
    return 0;
}

long ws_diff(const unsigned char *a, const unsigned char *b, int w, int h) {
    if (!a || !b || w <= 0 || h <= 0) return -1;
    long d = 0;
    for (long i = 0; i < (long)w * h; ++i)
        if (a[i] != b[i]) ++d;
    return d;
}

int ws_save_pgm(const char *path, const unsigned char *buf, int w, int h) {
    if (!path || !buf || w <= 0 || h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    return 0;
}
