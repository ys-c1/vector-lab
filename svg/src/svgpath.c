#include "svgpath.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 只认 ASCII 空白：isspace 的分类表来自 LC_CTYPE，换语言环境就可能把
 * 不同字节判成空白，解析结果随之变化。 */
static int ascii_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static const char *skip_sep(const char *p) {
    while (*p && (ascii_space((unsigned char)*p) || *p == ',')) ++p;
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
    /* strtod 的行为受 LC_NUMERIC 影响：锁 "C" 才能让 "." 永远是小数点，
     * 不随调用方进程的 locale 变化（库里不应假定 main 已经设过）。 */
    setlocale(LC_NUMERIC, "C");
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
