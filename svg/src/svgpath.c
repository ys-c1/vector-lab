#include "svgpath.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

#if SVG_REL

int svg_parse_ex(const char *d, VPath *out, int *err_seg) {
    int seg = 0;
#define FAIL(code) do { if (err_seg) *err_seg = seg; return (code); } while (0)
    if (!d || !out) return -1;
    out->count = 0;
    const char *p = skip_sep(d);
    if (!p || (*p != 'M' && *p != 'm')) { if (err_seg) *err_seg = 1; return -2; }
    VPoint cur = {0, 0}, start = {0, 0};
    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        ++seg;
        if (cmd != 'M' && cmd != 'm' && cmd != 'L' && cmd != 'l'
            && cmd != 'H' && cmd != 'h' && cmd != 'V' && cmd != 'v'
            && cmd != 'Z' && cmd != 'z') FAIL(-3);
        ++p;
        if (cmd == 'Z' || cmd == 'z') {
            if (out->count < 255) {
                VSeg s = {SV_LINE, cur, start};
                out->segs[out->count++] = s;
            }
            cur = start;
            continue;
        }
        double x = 0, y = 0;
        if (cmd == 'H' || cmd == 'h' || cmd == 'V' || cmd == 'v') {
            double v = 0;
            p = skip_sep(p);
            p = read_num(p, &v);
            if (!p) FAIL(-4);
            if (cmd == 'H')      { x = v;         y = cur.y; }
            else if (cmd == 'h') { x = cur.x + v; y = cur.y; }
            else if (cmd == 'V') { x = cur.x;     y = v; }
            else                 { x = cur.x;     y = cur.y + v; }
        } else {
            p = skip_sep(p);
            p = read_num(p, &x);
            if (!p) FAIL(-4);
            p = skip_sep(p);
            p = read_num(p, &y);
            if (!p) FAIL(-4);
            if (cmd == 'm' || cmd == 'l') { x += cur.x; y += cur.y; }
        }
        if (cmd == 'M' || cmd == 'm') {
            /* 每个 moveto 开启新子路径：Z 的闭合起点随之更新 */
            start.x = x; start.y = y;
            cur.x = x; cur.y = y;
            continue;
        }
        if (out->count < 255) {
            VSeg s = {SV_LINE, cur, {x, y}};
            out->segs[out->count++] = s;
        }
        cur.x = x; cur.y = y;
        if (out->count >= 255) FAIL(-5);
    }
    return 0;
#undef FAIL
}

int svg_parse(const char *d, VPath *out) { return svg_parse_ex(d, out, 0); }

#else /* !SVG_REL：初版解析器，原样保留 */

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

#endif /* SVG_REL */

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
