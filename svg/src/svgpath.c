#include "svgpath.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SVGPATH_MULTILAYER
#include <math.h>
#endif

static const char *skip_sep(const char *p) {
    while (*p && (isspace((unsigned char)*p) || *p == ',')) ++p;
    return p;
}

static const char *read_num(const char *p, double *v) {
    char *end = 0;
    double x = strtod(p, &end);
    if (end == p) return 0;
    *v = x;
    return end;
}

int svg_parse(const char *d, VPath *out) {
    if (!d || !out) return -1;
    out->count = 0;
    const char *p = skip_sep(d);
    if (!p || (*p != 'M' && *p != 'm')) return -2;
    VPoint cur = {0, 0}, start = {0, 0};
    int first = 1;
    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        if (cmd != 'M' && cmd != 'm' && cmd != 'L' && cmd != 'l'
            && cmd != 'Z' && cmd != 'z') return -3;
        ++p;
        if (cmd == 'Z' || cmd == 'z') {
            if (out->count < 255) {
                VSeg s = {SV_LINE, cur, start};
                out->segs[out->count++] = s;
            }
            cur = start;
            first = 0;
            continue;
        }
        double x = 0, y = 0;
        p = skip_sep(p);
        p = read_num(p, &x);
        if (!p) return -4;
        p = skip_sep(p);
        p = read_num(p, &y);
        if (!p) return -4;
        if (cmd == 'm' || cmd == 'l') { x += cur.x; y += cur.y; }
        if (first) { start.x = x; start.y = y; first = 0; }
        if (cmd == 'M' || cmd == 'm') { cur.x = x; cur.y = y; continue; }
        if (out->count < 255) {
            VSeg s = {SV_LINE, cur, {x, y}};
            out->segs[out->count++] = s;
        }
        cur.x = x; cur.y = y;
        if (out->count >= 255) return -5;
    }
    return 0;
}

int svg_fill(const VPath *p, int w, int h, unsigned char *buf) {
    if (!p || !buf || w <= 0 || h <= 0) return -1;
    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) {
        double sy = y + 0.5;
        for (int x = 0; x < w; ++x) {
            double sx = x + 0.5;
            int cross = 0;
            for (int i = 0; i < p->count; ++i) {
                VSeg s = p->segs[i];
                double y0 = s.a.y, y1 = s.b.y;
                if ((y0 > sy) == (y1 > sy)) continue;
                double t = (sy - y0) / (y1 - y0);
                double xc = s.a.x + (s.b.x - s.a.x) * t;
                if (xc > sx) ++cross;
            }
            if (cross & 1) buf[y * w + x] = 255;
        }
    }
    return 0;
}

int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h) {
    if (!path || !buf || w <= 0 || h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    return 0;
}

#ifdef SVGPATH_MULTILAYER
/* VPath 的 segs 只存线段（M 不产生段），子路径边界 = 段首与上段末不重合。*/
static int split_subpaths(const VPath *p, int *start, int *cnt, int maxsub) {
    int nsub = 0;
    int i = 0;
    while (i < p->count) {
        if (nsub >= maxsub) return -1;
        start[nsub] = i;
        int j = i + 1;
        while (j < p->count) {
            VPoint prev = p->segs[j - 1].b, cur = p->segs[j].a;
            double eps = 1e-12;
            if (fabs(prev.x - cur.x) > eps || fabs(prev.y - cur.y) > eps) break;
            ++j;
        }
        cnt[nsub] = j - i;
        ++nsub;
        i = j;
    }
    return nsub;
}

static int in_subpath(const VPath *p, int lo, int n, double sx, double sy) {
    int cross = 0;
    for (int k = 0; k < n; ++k) {
        VSeg s = p->segs[lo + k];
        double y0 = s.a.y, y1 = s.b.y;
        if ((y0 > sy) == (y1 > sy)) continue;
        double t = (sy - y0) / (y1 - y0);
        double xc = s.a.x + (s.b.x - s.a.x) * t;
        if (xc > sx) ++cross;
    }
    return cross & 1;
}

int svg_fill_layers(const VPath *p, int w, int h, unsigned char *buf) {
    if (!p || !buf || w <= 0 || h <= 0) return -1;
    int start[256], cnt[256];
    int nsub = split_subpaths(p, start, cnt, 256);
    if (nsub < 0) return -2;
    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) {
        double sy = y + 0.5;
        for (int x = 0; x < w; ++x) {
            double sx = x + 0.5;
            int depth = 0; /* 包含该像素的子轮廓数 = 嵌套层号（0 = 全在外）*/
            for (int s = 0; s < nsub; ++s)
                depth += in_subpath(p, start[s], cnt[s], sx, sy);
            if (depth == 0) continue;
            /* 每层不同灰阶，使内层轮廓可独立核验：255,204,153,... */
            int v = 255 - (depth - 1) * 51;
            buf[y * w + x] = (unsigned char)(v < 51 ? 51 : v);
        }
    }
    return 0;
}
#endif
