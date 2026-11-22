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

/* 与 gold/ref.c 相同的奇偶环绕判定，逐表达式对齐，保证 PGM 逐字节一致。 */
static int seg_inside(const VSeg *segs, int n, double x, double y) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        VPoint a = segs[i].a, b = segs[i].b;
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

int svg_fill(const VPath *p, int w, int h, unsigned char *buf) {
    if (!p || !buf || w <= 0 || h <= 0) return -1;
    int n = p->count;
    if (n == 0) return -2;
    /* 尺寸溢出检查：w*h 必须能在 size_t 内表示（图像本身即上限）。 */
    if ((size_t)w > (size_t)-1 / (size_t)h) return -1;
    size_t npix = (size_t)w * (size_t)h;
    memset(buf, 0, npix);

    double minx = p->segs[0].a.x, maxx = p->segs[0].a.x;
    double miny = p->segs[0].a.y, maxy = p->segs[0].a.y;
    for (int i = 0; i < n; ++i) {
        VPoint b = p->segs[i].b;
        if (b.x < minx) minx = b.x;
        if (b.x > maxx) maxx = b.x;
        if (b.y < miny) miny = b.y;
        if (b.y > maxy) maxy = b.y;
    }
    double sx = (maxx - minx) / w, sy = (maxy - miny) / h;
    const int SS = 8; /* 每像素 8x8=64 个子点 */
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int c = 0;
            for (int j = 0; j < SS; ++j)
                for (int i = 0; i < SS; ++i) {
                    double px = minx + (x + (i + 0.5) / SS) * sx;
                    double py = miny + (y + (j + 0.5) / SS) * sy;
                    if (seg_inside(p->segs, n, px, py)) ++c;
                }
            buf[y * w + x] = (unsigned char)(255 * c / (SS * SS));
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
