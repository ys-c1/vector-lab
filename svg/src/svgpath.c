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

#if SVG_SMOOTH
static VPoint cubic_eval(const VPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    VPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

static VPoint quad_eval(const VPoint q[3], double t) {
    double u = 1.0 - t;
    double a = u * u, b = 2 * u * t, c = t * t;
    VPoint p;
    p.x = a * q[0].x + b * q[1].x + c * q[2].x;
    p.y = a * q[0].y + b * q[1].y + c * q[2].y;
    return p;
}
#endif

int svg_parse2(const char *d, VPath *out, int subdiv, int *err_seg, VSeams *seams) {
#if !SVG_SMOOTH
    (void)subdiv; (void)err_seg; (void)seams;
    return svg_parse(d, out);
#else
    if (!d || !out) return -1;
    if (subdiv < 1) subdiv = 1;
    if (subdiv > 64) subdiv = 64;
    out->count = 0;
    if (seams) seams->count = 0;
    const char *p = skip_sep(d);
    if (!p || (*p != 'M' && *p != 'm')) return -2;
    VPoint cur = {0, 0}, start = {0, 0};
    VPoint last_c2 = {0, 0}, last_q1 = {0, 0};
    int prev = 0; /* 0 非曲线, 1 三次, 2 二次 */
    int segno = 0;
    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        if (!strchr("MmLlZzCcSsQqTt", cmd)) return -3;
        ++p;
        if (cmd == 'Z' || cmd == 'z') {
            if (out->count >= 255) return -5;
            VSeg s = {SV_LINE, cur, start};
            out->segs[out->count++] = s;
            ++segno;
            cur = start;
            prev = 0;
            continue;
        }
        double v[6];
        int need;
        char up = (char)toupper((unsigned char)cmd);
        if (up == 'M' || up == 'L' || up == 'T') need = 2;
        else if (up == 'S' || up == 'Q') need = 4;
        else need = 6;
        for (int i = 0; i < need; ++i) {
            p = skip_sep(p);
            p = read_num(p, &v[i]);
            if (!p) return -4;
        }
        if (cmd >= 'a') /* 相对命令 */
            for (int i = 0; i < need; i += 2) { v[i] += cur.x; v[i + 1] += cur.y; }
        if (up == 'M') { cur.x = v[0]; cur.y = v[1]; start = cur; prev = 0; continue; }
        if (up == 'L') {
            if (out->count >= 255) return -5;
            VSeg s = {SV_LINE, cur, {v[0], v[1]}};
            out->segs[out->count++] = s;
            ++segno;
            cur.x = v[0]; cur.y = v[1];
            prev = 0;
            continue;
        }
        VPoint c[4] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
        int is_cubic = (up == 'C' || up == 'S');
        if (up == 'C') {
            c[0] = cur; c[1].x = v[0]; c[1].y = v[1];
            c[2].x = v[2]; c[2].y = v[3]; c[3].x = v[4]; c[3].y = v[5];
        } else if (up == 'S') {
            if (prev != 1) { if (err_seg) *err_seg = segno; return -6; }
            c[0] = cur;
            c[1].x = 2 * cur.x - last_c2.x; c[1].y = 2 * cur.y - last_c2.y;
            c[2].x = v[0]; c[2].y = v[1]; c[3].x = v[2]; c[3].y = v[3];
        } else if (up == 'Q') {
            c[0] = cur; c[1].x = v[0]; c[1].y = v[1]; c[2].x = v[2]; c[2].y = v[3];
        } else { /* T */
            if (prev != 2) { if (err_seg) *err_seg = segno; return -6; }
            c[0] = cur;
            c[1].x = 2 * cur.x - last_q1.x; c[1].y = 2 * cur.y - last_q1.y;
            c[2].x = v[0]; c[2].y = v[1];
        }
        if (seams && (up == 'S' || up == 'T') && seams->count < 64)
            seams->at[seams->count++] = out->count;
        for (int i = 1; i <= subdiv; ++i) {
            VPoint q = is_cubic ? cubic_eval(c, (double)i / (double)subdiv)
                                : quad_eval(c, (double)i / (double)subdiv);
            if (out->count >= 255) return -5;
            VSeg s = {SV_LINE, cur, q};
            out->segs[out->count++] = s;
            cur = q;
        }
        ++segno;
        if (is_cubic) { last_c2 = c[2]; prev = 1; }
        else { last_q1 = c[1]; prev = 2; }
    }
    return 0;
#endif
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
