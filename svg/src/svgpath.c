#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SV_EPS 1e-9

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

static int num_start(char c) {
    return c == '.' || c == '-' || c == '+' || isdigit((unsigned char)c);
}

static int add_seg(VPath *out, VPoint a, VPoint b, int sub) {
    if (out->count >= SV_MAX_SEGS) return -1;
    VSeg s;
    s.kind = SV_LINE;
    s.a = a;
    s.b = b;
    s.sub = sub;
    out->segs[out->count++] = s;
    return 0;
}

static int finish_sub(VPath *out, int first, int n, int closed) {
    if (out->nsubs >= SV_MAX_SUBS) return -1;
    out->sub_first[out->nsubs] = first;
    out->sub_nseg[out->nsubs] = n;
    out->sub_closed[out->nsubs] = closed;
    out->nsubs++;
    return 0;
}

int svg_parse_diag(const char *d, VPath *out, VDiag *diag) {
    VDiag local;
    if (!diag) diag = &local;
    diag->code = SV_DIAG_NONE;
    diag->sub = 0;
    diag->pos = 0;
    if (!d || !out) return -1;

    out->count = 0;
    out->nsubs = 0;

    const char *p = skip_sep(d);
    if (!*p) {
        diag->code = SV_DIAG_EMPTY_PATH;
        return -2;
    }
    if (*p != 'M' && *p != 'm') {
        diag->code = SV_DIAG_SYNTAX;
        diag->pos = (int)(p - d);
        return -2;
    }

    VPoint cur = {0, 0}, start = {0, 0};
    int sub_seen = 0;              /* number of subpaths started so far */
    int cur_first = 0, cur_n = 0;  /* segment range of the open subpath */
    int cur_open = 0;

    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        if (!(cmd == 'M' || cmd == 'm' || cmd == 'L' || cmd == 'l'
              || cmd == 'Z' || cmd == 'z')) {
            diag->code = SV_DIAG_SYNTAX;
            diag->pos = (int)(p - d);
            return -3;
        }
        const char *cmd_at = p;
        ++p;

        if (cmd == 'Z' || cmd == 'z') {
            if (!cur_open) {
                diag->code = SV_DIAG_SYNTAX;
                diag->pos = (int)(cmd_at - d);
                return -3;
            }
            if (cur_n == 0) {
                diag->code = SV_DIAG_EMPTY_SUB;
                diag->sub = sub_seen;
                diag->pos = (int)(cmd_at - d);
                return -6;
            }
            if (finish_sub(out, cur_first, cur_n, 1) != 0) {
                diag->code = SV_DIAG_OVERFLOW;
                diag->pos = (int)(cmd_at - d);
                return -5;
            }
            cur_open = 0;
            cur = start;
            continue;
        }

        int is_move = (cmd == 'M' || cmd == 'm');
        int rel = (cmd == 'm' || cmd == 'l');
        if (is_move && cur_open) {
            /* A fresh M abandons the previous subpath without a Z. */
            diag->code = (cur_n == 0) ? SV_DIAG_EMPTY_SUB : SV_DIAG_OPEN_SUB;
            diag->sub = sub_seen;
            diag->pos = (int)(cmd_at - d);
            return (cur_n == 0) ? -6 : -7;
        }

        p = skip_sep(p);
        if (!num_start(*p)) {
            diag->code = SV_DIAG_SYNTAX;
            diag->pos = (int)(p - d);
            return -4;
        }

        for (;;) {
            double x = 0, y = 0;
            const char *q = read_num(p, &x);
            if (!q) {
                diag->code = SV_DIAG_SYNTAX;
                diag->pos = (int)(p - d);
                return -4;
            }
            p = skip_sep(q);
            q = read_num(p, &y);
            if (!q) {
                diag->code = SV_DIAG_SYNTAX;
                diag->pos = (int)(p - d);
                return -4;
            }
            p = q;
            if (rel) { x += cur.x; y += cur.y; }

            if (is_move) {
                ++sub_seen;
                cur_first = out->count;
                cur_n = 0;
                cur_open = 1;
                start.x = x; start.y = y;
                cur.x = x; cur.y = y;
                is_move = 0;   /* further pairs are implicit linetos */
            } else {
                if (!cur_open) {
                    diag->code = SV_DIAG_SYNTAX;
                    diag->pos = (int)(cmd_at - d);
                    return -3;
                }
                VPoint b = {x, y};
                if (add_seg(out, cur, b, sub_seen - 1) != 0) {
                    diag->code = SV_DIAG_OVERFLOW;
                    diag->pos = (int)(p - d);
                    return -5;
                }
                ++cur_n;
                cur.x = x; cur.y = y;
            }

            const char *next = skip_sep(p);
            if (!num_start(*next)) break;
            p = next;
        }
    }

    if (cur_open) {
        diag->code = (cur_n == 0) ? SV_DIAG_EMPTY_SUB : SV_DIAG_OPEN_SUB;
        diag->sub = sub_seen;
        diag->pos = (int)(p - d);   /* end of input: nothing closed it */
        return (cur_n == 0) ? -6 : -7;
    }
    if (out->nsubs == 0) {
        diag->code = SV_DIAG_EMPTY_PATH;
        return -2;
    }
    return 0;
}

int svg_parse(const char *d, VPath *out) {
    VDiag diag;
    return svg_parse_diag(d, out, &diag);
}

static int sub_edges(const VPath *p, int sub) {
    return p->sub_nseg[sub] + 1;   /* stored segments plus implicit closing edge */
}

static void sub_edge(const VPath *p, int sub, int k, VPoint *a, VPoint *b) {
    int f = p->sub_first[sub], n = p->sub_nseg[sub];
    if (k < n) {
        *a = p->segs[f + k].a;
        *b = p->segs[f + k].b;
    } else {
        *a = p->segs[f + n - 1].b;
        *b = p->segs[f].a;
    }
}

int svg_bbox(const VPath *p, VBox *out) {
    if (!p || !out || p->nsubs == 0) return -1;
    int have = 0;
    for (int s = 0; s < p->nsubs; ++s) {
        int f = p->sub_first[s], n = p->sub_nseg[s];
        for (int k = 0; k <= n; ++k) {
            VPoint v = (k < n) ? p->segs[f + k].a : p->segs[f + n - 1].b;
            if (!have) {
                out->minx = out->maxx = v.x;
                out->miny = out->maxy = v.y;
                have = 1;
            } else {
                if (v.x < out->minx) out->minx = v.x;
                if (v.x > out->maxx) out->maxx = v.x;
                if (v.y < out->miny) out->miny = v.y;
                if (v.y > out->maxy) out->maxy = v.y;
            }
        }
    }
    return have ? 0 : -1;
}

double svg_sub_signed_area(const VPath *p, int sub) {
    if (!p || sub < 0 || sub >= p->nsubs) return 0.0;
    int n = p->sub_nseg[sub];
    if (n < 1) return 0.0;
    double s = 0.0;
    for (int k = 0; k < sub_edges(p, sub); ++k) {
        VPoint a, b;
        sub_edge(p, sub, k, &a, &b);
        s += a.x * b.y - b.x * a.y;
    }
    return 0.5 * s;
}

int svg_winding_sub(const VPath *p, int sub, double x, double y) {
    if (!p || sub < 0 || sub >= p->nsubs) return 0;
    int w = 0;
    for (int k = 0; k < sub_edges(p, sub); ++k) {
        VPoint a, b;
        sub_edge(p, sub, k, &a, &b);
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        double xc = a.x + (b.x - a.x) * t;
        if (xc > x) w += (b.y > a.y) ? 1 : -1;
    }
    return w;
}

int svg_winding(const VPath *p, double x, double y, int *contrib) {
    if (!p) return 0;
    int w = 0;
    for (int s = 0; s < p->nsubs; ++s) {
        int c = svg_winding_sub(p, s, x, y);
        if (contrib) contrib[s] = c;
        w += c;
    }
    return w;
}

static int proper_cross(VPoint p1, VPoint p2, VPoint p3, VPoint p4) {
    double d1 = (p4.x - p3.x) * (p1.y - p3.y) - (p4.y - p3.y) * (p1.x - p3.x);
    double d2 = (p4.x - p3.x) * (p2.y - p3.y) - (p4.y - p3.y) * (p2.x - p3.x);
    double d3 = (p2.x - p1.x) * (p3.y - p1.y) - (p2.y - p1.y) * (p3.x - p1.x);
    double d4 = (p2.x - p1.x) * (p4.y - p1.y) - (p2.y - p1.y) * (p4.x - p1.x);
    if (d1 == 0.0 || d2 == 0.0 || d3 == 0.0 || d4 == 0.0) return 0;
    return ((d1 > 0) != (d2 > 0)) && ((d3 > 0) != (d4 > 0));
}

int svg_self_intersections(const VPath *p, int sub) {
    if (!p || sub < 0 || sub >= p->nsubs) return -1;
    int ne = sub_edges(p, sub);
    int count = 0;
    for (int i = 0; i < ne; ++i) {
        VPoint a0, a1;
        sub_edge(p, sub, i, &a0, &a1);
        for (int j = i + 1; j < ne; ++j) {
            if (j == i + 1 || (i == 0 && j == ne - 1)) continue;
            VPoint b0, b1;
            sub_edge(p, sub, j, &b0, &b1);
            if (proper_cross(a0, a1, b0, b1)) ++count;
        }
    }
    return count;
}

int svg_rule_signed_sum(const VPath *p) {
    if (!p) return 0;
    int sum = 0;
    for (int s = 0; s < p->nsubs; ++s) {
        double a = svg_sub_signed_area(p, s);
        if (fabs(a) > SV_EPS) sum += (a > 0.0) ? 1 : -1;
    }
    return sum;
}

int svg_rule(const VPath *p) {
    if (!p) return SV_FILL_EVENODD;
    int sum = svg_rule_signed_sum(p);
    int live = 0;
    for (int s = 0; s < p->nsubs; ++s)
        if (fabs(svg_sub_signed_area(p, s)) > SV_EPS) ++live;
    if (live > 0 && abs(sum) == live) return SV_FILL_NONZERO;
    return SV_FILL_EVENODD;
}

int svg_sample_filled(int rule, int winding) {
    if (rule == SV_FILL_NONZERO) return winding != 0;
    return (winding % 2) != 0;
}

double svg_area(const VPath *p, VBox box, int rule, int samples) {
    if (!p || samples < 1) return 0.0;
    if (!(box.maxx > box.minx) || !(box.maxy > box.miny)) return 0.0;
    if (rule == SV_FILL_AUTO) rule = svg_rule(p);
    double dx = (box.maxx - box.minx) / samples;
    double dy = (box.maxy - box.miny) / samples;
    long hit = 0;
    for (int j = 0; j < samples; ++j) {
        double py = box.miny + (j + 0.5) * dy;
        for (int i = 0; i < samples; ++i) {
            double px = box.minx + (i + 0.5) * dx;
            if (svg_sample_filled(rule, svg_winding(p, px, py, 0))) ++hit;
        }
    }
    return (double)hit / ((double)samples * samples)
           * (box.maxx - box.minx) * (box.maxy - box.miny);
}

static int all_closed(const VPath *p) {
    for (int s = 0; s < p->nsubs; ++s)
        if (!p->sub_closed[s]) return 0;
    return p->nsubs > 0;
}

int svg_raster(const VPath *p, VBox box, int w, int h, int ss, int rule,
               unsigned char *buf) {
    if (!p || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (!(box.maxx > box.minx) || !(box.maxy > box.miny)) return -1;
    if (!all_closed(p)) return -3;
    if (rule == SV_FILL_AUTO) rule = svg_rule(p);
    if (rule != SV_FILL_NONZERO && rule != SV_FILL_EVENODD) return -2;

    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box.maxx - box.minx) / w;
    double dy = (box.maxy - box.miny) / h;
    int total = ss * ss;

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j) {
                double py = box.miny + (y + (j + 0.5) / ss) * dy;
                for (int i = 0; i < ss; ++i) {
                    double px = box.minx + (x + (i + 0.5) / ss) * dx;
                    if (svg_sample_filled(rule, svg_winding(p, px, py, 0))) ++hit;
                }
            }
            buf[y * w + x] = (hit * 2 >= total) ? 255 : 0;
        }
    }
    return 0;
}

int svg_fill(const VPath *p, int w, int h, unsigned char *buf) {
    VBox box;
    if (!p || !buf || w <= 0 || h <= 0) return -1;
    box.minx = 0.0;
    box.miny = 0.0;
    box.maxx = (double)w;
    box.maxy = (double)h;
    return svg_raster(p, box, w, h, 4, SV_FILL_AUTO, buf);
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
