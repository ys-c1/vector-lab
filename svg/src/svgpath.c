#include "svgpath.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SVG_MAX_SEGS 256
#define SVG_MAX_DEPTH 24

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

/* True when a number (including "nan"/"inf") starts at p. */
static int starts_num(const char *p) {
    char *end = 0;
    (void)strtod(p, &end);
    return end != p;
}

static int fail(int *err_seg, int seg, int code) {
    if (err_seg) *err_seg = seg;
    return code;
}

/*
 * Distance from p to the segment ab.  This is used instead of the distance to
 * the infinite line so that control points projecting beyond the chord do not
 * under-estimate the flattening error.
 */
static double dist_seg(VPoint p, VPoint a, VPoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    double l2 = dx * dx + dy * dy;
    double t;
    if (l2 == 0.0) return hypot(p.x - a.x, p.y - a.y);
    t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / l2;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    return hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

static void mid(VPoint a, VPoint b, VPoint *m) {
    /* half first: avoids overflow when both coordinates are near DBL_MAX */
    m->x = 0.5 * a.x + 0.5 * b.x;
    m->y = 0.5 * a.y + 0.5 * b.y;
}

static int emit_line(VPath *out, VPoint a, VPoint b) {
    VSeg s;
    /* Same overflow point as the legacy line parser: 255 segments max. */
    if (out->count >= SVG_MAX_SEGS - 1) return -5;
    s.kind = SV_LINE;
    s.a = a;
    s.b = b;
    out->segs[out->count++] = s;
    return 0;
}

/*
 * Append the flattened cubic to *out.  c[0] is the current point, which the
 * path has already reached, so only sub-chords ending away from it are
 * emitted.  The recursion stops when both control points are within tol of
 * the chord, or at depth SVG_MAX_DEPTH so that degenerate input (all points
 * identical, huge coordinates) still terminates.
 */
static int flatten_cubic(const VPoint c[4], double tol, int depth, VPath *out) {
    int r;
    VPoint p01, p12, p23, p012, p123, m;
    VPoint left[4], right[4];
    if (depth >= SVG_MAX_DEPTH
        || (dist_seg(c[1], c[0], c[3]) <= tol
            && dist_seg(c[2], c[0], c[3]) <= tol))
        return emit_line(out, c[0], c[3]);
    mid(c[0], c[1], &p01);
    mid(c[1], c[2], &p12);
    mid(c[2], c[3], &p23);
    mid(p01, p12, &p012);
    mid(p12, p23, &p123);
    mid(p012, p123, &m);
    left[0] = c[0]; left[1] = p01; left[2] = p012; left[3] = m;
    right[0] = m; right[1] = p123; right[2] = p23; right[3] = c[3];
    r = flatten_cubic(left, tol, depth + 1, out);
    if (r != 0) return r;
    return flatten_cubic(right, tol, depth + 1, out);
}

/*
 * Read one cubic group (three coordinate pairs: control 1, control 2,
 * endpoint).  On success returns the position after the last number and fills
 * *g; on failure returns NULL and sets *err (-6 when fewer than three pairs
 * are present, -7 when a value is not finite).
 */
typedef struct { VPoint c1, c2, end; } CubicGroup;

static const char *read_cubic_group(const char *p, VPoint cur, int relative,
                                    CubicGroup *g, int *err) {
    double v[6];
    int i;
    VPoint base;
    p = skip_sep(p);
    for (i = 0; i < 6; ++i) {
        p = read_num(p, &v[i]);
        if (!p) { *err = SVG_ERR_CTRL_COUNT; return 0; }
        p = skip_sep(p);
    }
    for (i = 0; i < 6; ++i) {
        if (!isfinite(v[i])) { *err = SVG_ERR_NOT_FINITE; return 0; }
    }
    base = relative ? cur : (VPoint){0.0, 0.0};
    g->c1.x = base.x + v[0]; g->c1.y = base.y + v[1];
    g->c2.x = base.x + v[2]; g->c2.y = base.y + v[3];
    g->end.x = base.x + v[4];
    g->end.y = base.y + v[5];
    if (!isfinite(g->c1.x) || !isfinite(g->c1.y) || !isfinite(g->c2.x)
        || !isfinite(g->c2.y) || !isfinite(g->end.x) || !isfinite(g->end.y)) {
        *err = SVG_ERR_NOT_FINITE;
        return 0;
    }
    *err = 0;
    return p;
}

const char *svg_strerror(int code) {
    switch (code) {
    case 0: return "ok";
    case -1: return "null argument";
    case -2: return "path must start with M/m";
    case -3: return "unsupported command (C/c present without cubic enabled)";
    case -4: return "missing or invalid coordinate";
    case -5: return "too many segments";
    case SVG_ERR_CTRL_COUNT: return "C/c group must have exactly 3 coordinate pairs";
    case SVG_ERR_NOT_FINITE: return "coordinate is not finite (NaN or infinity)";
    case SVG_ERR_TOL: return "tolerance must be finite and > 0";
    default: return "unknown error";
    }
}

/*
 * Segment numbering: drawing commands are numbered from 1 in input order.
 * M/m is a move, not a drawing segment and takes no number; L/l, each C/c
 * letter and Z/z each take one.  Each C/c letter is exactly one group, so it
 * is one number too.  An error inside M/m reports the number the next drawing
 * segment would get.  0 means the error is not tied to any command (null
 * argument, bad start).
 */

int svg_parse_ex(const char *d, VPath *out, const VParseOpt *opt, int *err_seg) {
    const char *p;
    VPoint cur = {0.0, 0.0}, start = {0.0, 0.0};
    int first = 1;
    int cubic = 0, seg_no = 0;
    double tol = 0.0;

    if (err_seg) *err_seg = 0;
    if (!d || !out) return -1;
    if (opt && opt->enable_cubic) {
        cubic = 1;
        tol = opt->tol;
        if (!isfinite(tol) || tol <= 0.0) return SVG_ERR_TOL;
    }
    out->count = 0;
    p = skip_sep(d);
    if (!p || (*p != 'M' && *p != 'm')) return fail(err_seg, 0, -2);

    while (*p) {
        char cmd;
        int here;
        p = skip_sep(p);
        if (!*p) break;
        cmd = *p;
        if (cmd != 'M' && cmd != 'm' && cmd != 'L' && cmd != 'l'
            && cmd != 'Z' && cmd != 'z' && cmd != 'C' && cmd != 'c')
            return fail(err_seg, seg_no + 1, -3);
        ++p;
        /* M/m is a move, not a drawing segment: it does not advance seg_no. */
        if (cmd == 'M' || cmd == 'm') {
            here = seg_no + 1;
        } else {
            here = ++seg_no;
        }

        if (cmd == 'Z' || cmd == 'z') {
            if (out->count < 255) {
                VSeg s = {SV_LINE, cur, start};
                out->segs[out->count++] = s;
            }
            cur = start;
            first = 0;
            continue;
        }

        if (cmd == 'C' || cmd == 'c') {
            CubicGroup g;
            VPoint c4[4];
            const char *q;
            int err = 0;
            if (!cubic) return fail(err_seg, here, -3);
            p = skip_sep(p);
            q = read_cubic_group(p, cur, cmd == 'c', &g, &err);
            if (!q) return fail(err_seg, here, err);
            /* Exactly one group per C letter; extra numbers are a count error. */
            p = skip_sep(q);
            if (starts_num(p)) return fail(err_seg, here, SVG_ERR_CTRL_COUNT);
            c4[0] = cur;
            c4[1] = g.c1;
            c4[2] = g.c2;
            c4[3] = g.end;
            if (flatten_cubic(c4, tol, 0, out) != 0)
                return fail(err_seg, here, -5);
            cur = c4[3];
            first = 0;
            continue;
        }

        /* Legacy M/m/L/l handling, unchanged from the line-only parser. */
        {
            double x = 0.0, y = 0.0;
            p = skip_sep(p);
            p = read_num(p, &x);
            if (!p) return fail(err_seg, here, -4);
            p = skip_sep(p);
            p = read_num(p, &y);
            if (!p) return fail(err_seg, here, -4);
            if (cmd == 'm' || cmd == 'l') { x += cur.x; y += cur.y; }
            if (first) { start.x = x; start.y = y; first = 0; }
            if (cmd == 'M' || cmd == 'm') { cur.x = x; cur.y = y; continue; }
            if (out->count < 255) {
                VSeg s = {SV_LINE, cur, {x, y}};
                out->segs[out->count++] = s;
            }
            cur.x = x; cur.y = y;
            if (out->count >= 255) return fail(err_seg, here, -5);
        }
    }
    return 0;
}

int svg_parse(const char *d, VPath *out) {
    return svg_parse_ex(d, out, 0, 0);
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
