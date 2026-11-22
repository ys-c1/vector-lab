#include "svgpath.h"
#include <ctype.h>
#include <math.h>
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

/* ---- viewBox 单位换算 ---- */

static void set_field(char err_field[SV_FIELD_MAX], const char *name) {
    if (!err_field) return;
    if (!name) name = "";
    snprintf(err_field, SV_FIELD_MAX, "%s", name);
}

static const char *vb_name(int i) {
    static const char *names[4] = {
        "viewBox.minx", "viewBox.miny", "viewBox.w", "viewBox.h"
    };
    return (i >= 0 && i < 4) ? names[i] : "viewBox";
}

int svg_parse_viewbox(const char *s, VBox *out, char err_field[SV_FIELD_MAX]) {
    if (!s || !out) {
        set_field(err_field, "viewBox");
        return SV_E_ARG;
    }
    double v[4];
    const char *p = s;
    for (int i = 0; i < 4; ++i) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) ++p;
        char *end = 0;
        double x = strtod(p, &end);
        if (end == p) {
            set_field(err_field, vb_name(i));
            return SV_E_VIEWBOX;
        }
        if (!isfinite(x)) {
            set_field(err_field, vb_name(i));
            return SV_E_VIEWBOX;
        }
        v[i] = x;
        p = end;
    }
    /* 多余的数字/字母说明串写错了 */
    while (*p && (isspace((unsigned char)*p) || *p == ',')) ++p;
    if (*p) {
        set_field(err_field, "viewBox");
        return SV_E_VIEWBOX;
    }
    out->minx = v[0];
    out->miny = v[1];
    out->w = v[2];
    out->h = v[3];
    return SV_OK;
}

int svg_map_fit(const VBox *vb, int w, int h, VMap *out,
                char err_field[SV_FIELD_MAX]) {
    if (!vb) { set_field(err_field, "viewBox"); return SV_E_ARG; }
    if (!out) { set_field(err_field, "out"); return SV_E_ARG; }
    if (!isfinite(vb->minx)) { set_field(err_field, "viewBox.minx"); return SV_E_VIEWBOX; }
    if (!isfinite(vb->miny)) { set_field(err_field, "viewBox.miny"); return SV_E_VIEWBOX; }
    if (!isfinite(vb->w) || vb->w <= 0.0) { set_field(err_field, "viewBox.w"); return SV_E_VIEWBOX; }
    if (!isfinite(vb->h) || vb->h <= 0.0) { set_field(err_field, "viewBox.h"); return SV_E_VIEWBOX; }
    if (w <= 0) { set_field(err_field, "target.w"); return SV_E_TARGET; }
    if (h <= 0) { set_field(err_field, "target.h"); return SV_E_TARGET; }

    out->sx = (double)w / vb->w;
    out->sy = (double)h / vb->h;
    out->tx = -vb->minx * out->sx;
    out->ty = -vb->miny * out->sy;
    out->aspect_err = fabs(out->sx / out->sy - 1.0);
    out->edge_px = 0.5 * sqrt(2.0);
    /* 各轴半像素折回用户单位后的欧氏上界 */
    double ux = 0.5 / out->sx, uy = 0.5 / out->sy;
    out->edge_user = sqrt(ux * ux + uy * uy);
    return SV_OK;
}

VPoint svg_map_apply(const VMap *m, VPoint p) {
    VPoint q;
    q.x = m->sx * p.x + m->tx;
    q.y = m->sy * p.y + m->ty;
    return q;
}

VPoint svg_map_sample(const VMap *m, int px, int py) {
    VPoint q;
    q.x = (px + 0.5 - m->tx) / m->sx;
    q.y = (py + 0.5 - m->ty) / m->sy;
    return q;
}

int svg_fill_mapped(const VPath *p, const VBox *vb, int w, int h,
                    unsigned flags, unsigned char *buf,
                    char err_field[SV_FIELD_MAX]) {
    if (!p) { set_field(err_field, "path"); return SV_E_ARG; }
    if (!buf) { set_field(err_field, "buf"); return SV_E_ARG; }
    if (w <= 0) { set_field(err_field, "target.w"); return SV_E_TARGET; }
    if (h <= 0) { set_field(err_field, "target.h"); return SV_E_TARGET; }
    if (flags & ~SV_F_VIEWBOX) { set_field(err_field, "flags"); return SV_E_FLAGS; }
    if (!(flags & SV_F_VIEWBOX)) return svg_fill(p, w, h, buf);

    VMap m;
    int rc = svg_map_fit(vb, w, h, &m, err_field);
    if (rc != SV_OK) return rc;

    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            VPoint s = svg_map_sample(&m, x, y);
            int cross = 0;
            for (int i = 0; i < p->count; ++i) {
                VSeg g = p->segs[i];
                if ((g.a.y > s.y) == (g.b.y > s.y)) continue;
                double t = (s.y - g.a.y) / (g.b.y - g.a.y);
                double xc = g.a.x + (g.b.x - g.a.x) * t;
                if (xc > s.x) ++cross;
            }
            if (cross & 1) buf[y * w + x] = 255;
        }
    }
    return SV_OK;
}

