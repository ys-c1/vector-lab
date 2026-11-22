#include "svgpath.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SVG_PI 3.14159265358979323846

static SvgError g_err;

static void err_clear(int code) {
    g_err.code = code;
    g_err.seg = -1;
    g_err.field[0] = '\0';
    g_err.value = NAN;
    g_err.msg[0] = '\0';
}

static void err_set(int code, int seg, const char *field, double value,
                    const char *msg) {
    g_err.code = code;
    g_err.seg = seg;
    if (field) {
        strncpy(g_err.field, field, sizeof g_err.field - 1);
        g_err.field[sizeof g_err.field - 1] = '\0';
    } else {
        g_err.field[0] = '\0';
    }
    g_err.value = value;
    if (msg) {
        strncpy(g_err.msg, msg, sizeof g_err.msg - 1);
        g_err.msg[sizeof g_err.msg - 1] = '\0';
    } else {
        g_err.msg[0] = '\0';
    }
}

int svg_last_error(void) { return g_err.code; }
const SvgError *svg_last_error_detail(void) { return &g_err; }

const char *svg_strerror(int code) {
    switch (code) {
    case SVG_OK:           return "ok";
    case SVG_E_ARG:        return "invalid argument";
    case SVG_E_START:      return "path must start with M/m";
    case SVG_E_CMD:        return "unsupported command";
    case SVG_E_NUM:        return "missing or invalid number";
    case SVG_E_CAP:        return "capacity exceeded";
    case SVG_E_RADIUS:     return "ellipse radius <= 0 or too small for endpoints";
    case SVG_E_COINCIDENT: return "ellipse arc endpoints coincide";
    case SVG_E_XROT:       return "x-axis rotation outside [-90, 90] degrees";
    case SVG_E_NONFINITE:  return "non-finite arc parameter";
    case SVG_E_FLAGS:      return "unknown flags";
    case SVG_E_ARC:        return "ellipse arc parameterization failed";
    case SVG_E_IO:         return "I/O error";
    default:               return "unknown error";
    }
}

/* ------------------------------------------------------------------ */
/* 解析                                                               */
/* ------------------------------------------------------------------ */

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

/* 弧标志是单个字符 0 或 1；允许与后面的数字紧贴（"0110" 拆成 0 1 110）。 */
static const char *read_flag(const char *p, int *v) {
    p = skip_sep(p);
    if (*p == '0' || *p == '1') {
        *v = *p - '0';
        return p + 1;
    }
    return 0;
}

static int finite3(double x, double y, double z) {
    return isfinite(x) && isfinite(y) && isfinite(z);
}

static int parse_impl(const char *d, VPath *out, unsigned flags) {
    if (!d || !out) {
        err_set(SVG_E_ARG, -1, "out", NAN, "null path or output");
        return SVG_E_ARG;
    }
    out->count = 0;
    const char *p = skip_sep(d);
    if (!p || (*p != 'M' && *p != 'm')) {
        err_set(SVG_E_START, -1, "", NAN, "path must start with M/m");
        return SVG_E_START;
    }
    VPoint cur = {0, 0}, start = {0, 0};
    int first = 1;
    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        int is_arc = (cmd == 'A' || cmd == 'a');
        if (cmd != 'M' && cmd != 'm' && cmd != 'L' && cmd != 'l'
            && cmd != 'Z' && cmd != 'z' && !is_arc) {
            err_set(SVG_E_CMD, out->count, "", NAN, "unsupported command");
            return SVG_E_CMD;
        }
        if (is_arc && !(flags & SVG_F_ARCS)) {
            err_set(SVG_E_CMD, out->count, "A", NAN,
                    "arc command requires SVG_F_ARCS");
            return SVG_E_CMD;
        }
        ++p;
        if (cmd == 'Z' || cmd == 'z') {
            if (out->count < 255) {
                VSeg s;
                memset(&s, 0, sizeof s);
                s.kind = SV_LINE;
                s.a = cur;
                s.b = start;
                out->segs[out->count++] = s;
            }
            cur = start;
            first = 0;
            continue;
        }

        if (is_arc) {
            double rx = 0, ry = 0, xrot_deg = 0, x = 0, y = 0;
            int large = 0, sweep = 0;
            int segno = out->count;

            p = skip_sep(p);
            p = read_num(p, &rx);
            if (!p) { err_set(SVG_E_NUM, segno, "rx", NAN, "arc: missing rx"); return SVG_E_NUM; }
            p = skip_sep(p);
            p = read_num(p, &ry);
            if (!p) { err_set(SVG_E_NUM, segno, "ry", NAN, "arc: missing ry"); return SVG_E_NUM; }
            p = skip_sep(p);
            p = read_num(p, &xrot_deg);
            if (!p) { err_set(SVG_E_NUM, segno, "xrot", NAN, "arc: missing xrot"); return SVG_E_NUM; }
            p = read_flag(p, &large);
            if (!p) { err_set(SVG_E_NUM, segno, "large", NAN, "arc: large-arc-flag must be 0 or 1"); return SVG_E_NUM; }
            p = read_flag(p, &sweep);
            if (!p) { err_set(SVG_E_NUM, segno, "sweep", NAN, "arc: sweep-flag must be 0 or 1"); return SVG_E_NUM; }
            p = skip_sep(p);
            p = read_num(p, &x);
            if (!p) { err_set(SVG_E_NUM, segno, "x", NAN, "arc: missing x"); return SVG_E_NUM; }
            p = skip_sep(p);
            p = read_num(p, &y);
            if (!p) { err_set(SVG_E_NUM, segno, "y", NAN, "arc: missing y"); return SVG_E_NUM; }

            if (cmd == 'a') { x += cur.x; y += cur.y; }

            if (!finite3(rx, ry, xrot_deg) || !finite3(x, y, 0.0)) {
                const char *f = "rx";
                double v = rx;
                if (!isfinite(rx)) { f = "rx"; v = rx; }
                else if (!isfinite(ry)) { f = "ry"; v = ry; }
                else if (!isfinite(xrot_deg)) { f = "xrot"; v = xrot_deg; }
                else if (!isfinite(x)) { f = "x"; v = x; }
                else { f = "y"; v = y; }
                err_set(SVG_E_NONFINITE, segno, f, v, "arc: non-finite parameter");
                return SVG_E_NONFINITE;
            }
            /* 几何合法性（半径、旋转角、起止点重合）留到参数化时检查：
             * 兼容整圆模式不看这些量，不能因为打开 A/a 解析就拒绝一条
             * 现状能画出来的整圆。 */

            if (out->count < 255) {
                VSeg s;
                memset(&s, 0, sizeof s);
                s.kind = SV_ARC;
                s.a = cur;
                s.b = (VPoint){x, y};
                s.rx = rx;
                s.ry = ry;
                s.xrot = xrot_deg * SVG_PI / 180.0;
                s.large = large;
                s.sweep = sweep;
                out->segs[out->count++] = s;
            }
            if (first) { start.x = x; start.y = y; first = 0; }
            cur.x = x; cur.y = y;
            if (out->count >= 255) {
                err_set(SVG_E_CAP, out->count - 1, "", NAN, "too many segments");
                return SVG_E_CAP;
            }
            continue;
        }

        double x = 0, y = 0;
        p = skip_sep(p);
        p = read_num(p, &x);
        if (!p) { err_set(SVG_E_NUM, out->count, "x", NAN, "missing x"); return SVG_E_NUM; }
        p = skip_sep(p);
        p = read_num(p, &y);
        if (!p) { err_set(SVG_E_NUM, out->count, "y", NAN, "missing y"); return SVG_E_NUM; }
        if (cmd == 'm' || cmd == 'l') { x += cur.x; y += cur.y; }
        if (first) { start.x = x; start.y = y; first = 0; }
        if (cmd == 'M' || cmd == 'm') { cur.x = x; cur.y = y; continue; }
        if (out->count < 255) {
            VSeg s;
            memset(&s, 0, sizeof s);
            s.kind = SV_LINE;
            s.a = cur;
            s.b = (VPoint){x, y};
            out->segs[out->count++] = s;
        }
        cur.x = x; cur.y = y;
        if (out->count >= 255) {
            err_set(SVG_E_CAP, out->count - 1, "", NAN, "too many segments");
            return SVG_E_CAP;
        }
    }
    err_clear(SVG_OK);
    return SVG_OK;
}

int svg_parse(const char *d, VPath *out) {
    return parse_impl(d, out, 0);
}

int svg_parse_flags(const char *d, VPath *out, unsigned flags) {
    if (flags & ~SVG_F_ARCS) {
        err_set(SVG_E_FLAGS, -1, "flags", (double)flags, "unknown flags");
        return SVG_E_FLAGS;
    }
    return parse_impl(d, out, flags);
}

/* ------------------------------------------------------------------ */
/* 椭圆弧参数化（SVG 1.1 F.6.5）                                       */
/* ------------------------------------------------------------------ */

int svg_arc_frame(int seg, const VSeg *s, SvgArcFrame *out) {
    if (!s || !out) {
        err_set(SVG_E_ARG, seg, "out", NAN, "null arc or output");
        return SVG_E_ARG;
    }
    if (s->kind != SV_ARC) {
        err_set(SVG_E_ARG, seg, "kind", (double)s->kind, "segment is not an arc");
        return SVG_E_ARG;
    }
    double rx = s->rx, ry = s->ry, phi = s->xrot;
    if (!finite3(rx, ry, phi) || !isfinite(s->a.x) || !isfinite(s->a.y)
        || !isfinite(s->b.x) || !isfinite(s->b.y)) {
        err_set(SVG_E_NONFINITE, seg, "rx/ry/xrot/a/b", NAN,
                "arc: non-finite parameter");
        return SVG_E_NONFINITE;
    }
    if (rx < 0.0 || ry < 0.0) {
        /* SVG F.6.2：负半径按绝对值处理，不报错 */
        rx = fabs(rx);
        ry = fabs(ry);
    }
    if (rx == 0.0) {
        err_set(SVG_E_RADIUS, seg, "rx", rx, "arc: rx must be non-zero");
        return SVG_E_RADIUS;
    }
    if (ry == 0.0) {
        err_set(SVG_E_RADIUS, seg, "ry", ry, "arc: ry must be non-zero");
        return SVG_E_RADIUS;
    }
    /* 旋转角按 180° 周期取到 [-90°, 90°]；越界即报错。 */
    double xrot_deg = phi * 180.0 / SVG_PI;
    if (xrot_deg > 90.0 + 1e-9 || xrot_deg < -90.0 - 1e-9) {
        err_set(SVG_E_XROT, seg, "xrot", xrot_deg,
                "arc: x-axis rotation outside [-90, 90]");
        return SVG_E_XROT;
    }
    if (s->large != 0 && s->large != 1) {
        err_set(SVG_E_ARG, seg, "large", (double)s->large, "arc: large flag must be 0/1");
        return SVG_E_ARG;
    }
    if (s->sweep != 0 && s->sweep != 1) {
        err_set(SVG_E_ARG, seg, "sweep", (double)s->sweep, "arc: sweep flag must be 0/1");
        return SVG_E_ARG;
    }
    if (s->a.x == s->b.x && s->a.y == s->b.y) {
        err_set(SVG_E_COINCIDENT, seg, "x/y", NAN,
                "arc: start and end points coincide");
        return SVG_E_COINCIDENT;
    }

    double c = cos(phi), sn = sin(phi);
    double dx2 = (s->a.x - s->b.x) / 2.0;
    double dy2 = (s->a.y - s->b.y) / 2.0;
    double x1p =  c * dx2 + sn * dy2;
    double y1p = -sn * dx2 + c * dy2;

    double lambda = x1p * x1p / (rx * rx) + y1p * y1p / (ry * ry);
    double scale = 1.0;
    int radius_err = 0;
    if (lambda > 1.0 + 1e-9) {
        scale = sqrt(lambda);
        radius_err = 1;
    } else if (lambda > 1.0) {
        scale = sqrt(lambda);   /* 仅在容差内，放大后仍精确过端点 */
    }
    double erx = rx * scale, ery = ry * scale;

    double num = erx * erx * ery * ery - erx * erx * y1p * y1p
               - ery * ery * x1p * x1p;
    double den = erx * erx * y1p * y1p + ery * ery * x1p * x1p;
    if (den <= 0.0) {
        err_set(SVG_E_ARC, seg, "rx/ry", NAN, "arc: degenerate parameterization");
        return SVG_E_ARC;
    }
    if (num < 0.0) num = 0.0;   /* 理论上 num>=0；容差内吸收舍入 */
    double coef = sqrt(num / den);
    if (s->large == s->sweep) coef = -coef;

    double cxp =  coef * erx * y1p / ery;
    double cyp = -coef * ery * x1p / erx;

    double cx = c * cxp - sn * cyp + (s->a.x + s->b.x) / 2.0;
    double cy = sn * cxp + c * cyp + (s->a.y + s->b.y) / 2.0;

    double ux = (x1p - cxp) / erx, uy = (y1p - cyp) / ery;
    double vx = (-x1p - cxp) / erx, vy = (-y1p - cyp) / ery;

    double theta1 = atan2(uy, ux);
    double dtheta = atan2(ux * vy - uy * vx, ux * vx + uy * vy);
    if (s->sweep) {
        if (dtheta < 0.0) dtheta += 2.0 * SVG_PI;
    } else {
        if (dtheta > 0.0) dtheta -= 2.0 * SVG_PI;
    }

    out->center.x = cx;
    out->center.y = cy;
    out->rx = erx;
    out->ry = ery;
    out->scale = scale;
    out->xrot = phi;
    out->theta1 = theta1;
    out->dtheta = dtheta;
    out->theta2 = theta1 + dtheta;

    if (radius_err) {
        err_set(SVG_E_RADIUS, seg, "rx/ry", scale,
                "arc: radii too small to connect endpoints (scale factor given)");
        return SVG_E_RADIUS;
    }
    err_clear(SVG_OK);
    return SVG_OK;
}

/* 每段弦的参数角与两档弦数。rate 用 max(rx,ry) 作统一曲率半径。 */
static void chord_plan(const SvgArcFrame *f, double tol, double span,
                       int *n, double *bound, int *nplus, int *nminus) {
    double rmax = f->rx > f->ry ? f->rx : f->ry;
    double hmax = sqrt(8.0 * tol / rmax);
    double q = fabs(span) / hmax;
    int np = (int)ceil(q);
    if (np < 1) np = 1;
    int nm = (int)floor(q);
    if (nm < 1) nm = 1;
    if (np > SVG_ARC_CHORD_CAP) np = SVG_ARC_CHORD_CAP;
    if (nm > SVG_ARC_CHORD_CAP) nm = SVG_ARC_CHORD_CAP;
    *nplus = np;
    *nminus = nm;
    *n = np;
    *bound = rmax * (fabs(span) / (double)np) * (fabs(span) / (double)np) / 8.0;
}

int svg_arc_chords(int seg, const VSeg *s, double tol,
                   int *n, double *bound, int *nplus, int *nminus) {
    if (!s || !n || !bound) {
        err_set(SVG_E_ARG, seg, "out", NAN, "null arc or output");
        return SVG_E_ARG;
    }
    if (!isfinite(tol) || tol <= 0.0) {
        err_set(SVG_E_ARG, seg, "tol", tol, "tol must be finite and > 0");
        return SVG_E_ARG;
    }
    SvgArcFrame f;
    int rc = svg_arc_frame(seg, s, &f);
    if (rc != SVG_OK) return rc;
    int np, nm;
    double b;
    chord_plan(&f, tol, f.dtheta, n, &b, &np, &nm);
    *bound = b;
    if (nplus) *nplus = np;
    if (nminus) *nminus = nm;
    err_clear(SVG_OK);
    return SVG_OK;
}

static VPoint arc_point(const SvgArcFrame *f, double t) {
    double c = cos(f->xrot), sn = sin(f->xrot);
    double u = f->rx * cos(t), v = f->ry * sin(t);
    VPoint p;
    p.x = f->center.x + c * u - sn * v;
    p.y = f->center.y + sn * u + c * v;
    return p;
}

int svg_arc_polyline(int seg, const VSeg *s, double tol,
                     VPoint *pts, int cap, int *n, double *bound) {
    if (!s || !pts || cap < 2) {
        err_set(SVG_E_ARG, seg, "pts", NAN, "null points or capacity < 2");
        return SVG_E_ARG;
    }
    if (!isfinite(tol) || tol <= 0.0) {
        err_set(SVG_E_ARG, seg, "tol", tol, "tol must be finite and > 0");
        return SVG_E_ARG;
    }
    SvgArcFrame f;
    int rc = svg_arc_frame(seg, s, &f);
    if (rc != SVG_OK) return rc;
    int nn, np, nm;
    double b;
    chord_plan(&f, tol, f.dtheta, &nn, &b, &np, &nm);
    if (nn + 1 > cap) {
        err_set(SVG_E_CAP, seg, "cap", (double)cap, "polyline capacity exceeded");
        return SVG_E_CAP;
    }
    for (int k = 0; k <= nn; ++k) {
        if (k == 0) pts[k] = s->a;    /* 首端精确落在弧起点上 */
        else if (k == nn) pts[k] = s->b;  /* 末端精确落在弧终点上 */
        else pts[k] = arc_point(&f, f.theta1 + f.dtheta * (double)k / (double)nn);
    }
    if (n) *n = nn;
    if (bound) *bound = b;
    err_clear(SVG_OK);
    return SVG_OK;
}

/* ------------------------------------------------------------------ */
/* 填充                                                               */
/* ------------------------------------------------------------------ */

typedef struct { double x0, y0, x1, y1; } Edge;

typedef struct {
    Edge *edges;
    long count, cap;
    unsigned char *buf;
    int w, h;
} FillCtx;

/* 边表按需增长：小图形不预付整张 SVG_EDGE_CAP 的内存。 */
static int push_edge(FillCtx *c, VPoint a, VPoint b) {
    if (c->count >= c->cap) {
        if (c->cap >= SVG_EDGE_CAP) return SVG_E_CAP;
        long ncap = c->cap * 2 > SVG_EDGE_CAP ? SVG_EDGE_CAP : c->cap * 2;
        Edge *p = (Edge *)realloc(c->edges, sizeof(Edge) * (size_t)ncap);
        if (!p) return SVG_E_ARG;
        c->edges = p;
        c->cap = ncap;
    }
    c->edges[c->count++] = (Edge){a.x, a.y, b.x, b.y};
    return SVG_OK;
}

/* 兼容整圆：圆心在弧起点 a，半径 rx；用与椭圆弧相同的容差公式细分。 */
static int push_circle(FillCtx *c, int seg, const VSeg *s, double tol,
                       SvgArcRow *row, double *bound_out) {
    if (!isfinite(s->a.x) || !isfinite(s->a.y) || !isfinite(s->rx) || s->rx <= 0.0) {
        err_set(SVG_E_RADIUS, seg, "rx", s->rx, "compat circle needs finite rx > 0");
        return SVG_E_RADIUS;
    }
    SvgArcFrame f;
    f.center = s->a;
    f.rx = s->rx;
    f.ry = s->rx;
    f.scale = 1.0;
    f.xrot = 0.0;
    f.theta1 = 0.0;
    f.dtheta = 2.0 * SVG_PI;
    f.theta2 = 2.0 * SVG_PI;
    int nn, np, nm;
    double b;
    chord_plan(&f, tol, f.dtheta, &nn, &b, &np, &nm);
    for (int k = 0; k < nn; ++k) {
        VPoint p0 = arc_point(&f, 2.0 * SVG_PI * (double)k / (double)nn);
        VPoint p1 = arc_point(&f, 2.0 * SVG_PI * (double)(k + 1) / (double)nn);
        int rc = push_edge(c, p0, p1);
        if (rc != SVG_OK) {
            err_set(rc, seg, "edges", (double)c->cap, "edge capacity exceeded");
            return rc;
        }
    }
    if (row) {
        row->seg = seg;
        row->whole = 1;
        row->center = f.center;
        row->rx = f.rx;
        row->ry = f.ry;
        row->xrot = 0.0;
        row->theta1 = f.theta1;
        row->dtheta = f.dtheta;
        row->theta2 = f.theta2;
        row->bound = b;
        row->nplus = np;
        row->nminus = nm;
        row->n = nn;
    }
    if (bound_out) *bound_out = b;
    return SVG_OK;
}

static int push_ellipse(FillCtx *c, int seg, const VSeg *s, double tol,
                        int compact, SvgArcRow *row, double *bound_out) {
    SvgArcFrame f;
    int rc = svg_arc_frame(seg, s, &f);
    if (rc != SVG_OK) return rc;   /* 错误详情（段号、字段、数值）已由 frame 填好 */
    int nn, np, nm;
    double b;
    chord_plan(&f, tol, f.dtheta, &nn, &b, &np, &nm);
    if (compact) nn = nm;
    double b_used = f.rx > f.ry ? f.rx : f.ry;
    b_used = b_used * (fabs(f.dtheta) / (double)nn) * (fabs(f.dtheta) / (double)nn) / 8.0;

    VPoint prev = s->a;
    for (int k = 1; k <= nn; ++k) {
        VPoint q = (k == nn) ? s->b
                             : arc_point(&f, f.theta1 + f.dtheta * (double)k / (double)nn);
        rc = push_edge(c, prev, q);
        if (rc != SVG_OK) {
            err_set(rc, seg, "edges", (double)c->cap, "edge capacity exceeded");
            return rc;
        }
        prev = q;
    }
    if (row) {
        row->seg = seg;
        row->whole = 0;
        row->center = f.center;
        row->rx = f.rx;
        row->ry = f.ry;
        row->xrot = f.xrot;
        row->theta1 = f.theta1;
        row->dtheta = f.dtheta;
        row->theta2 = f.theta2;
        row->bound = b_used;
        row->nplus = np;
        row->nminus = nm;
        row->n = nn;
    }
    if (bound_out) *bound_out = b_used;
    return SVG_OK;
}

/* 填充核心：逐行交叉奇偶。对纯线段路径，运算顺序与旧实现逐位相同。 */
static void fill_scan(const FillCtx *c) {
    int w = c->w, h = c->h;
    unsigned char *buf = c->buf;
    for (int y = 0; y < h; ++y) {
        double sy = y + 0.5;
        for (int x = 0; x < w; ++x) {
            double sx = x + 0.5;
            int cross = 0;
            for (long i = 0; i < c->count; ++i) {
                const Edge *e = &c->edges[i];
                double y0 = e->y0, y1 = e->y1;
                if ((y0 > sy) == (y1 > sy)) continue;
                double t = (sy - y0) / (y1 - y0);
                double xc = e->x0 + (e->x1 - e->x0) * t;
                if (xc > sx) ++cross;
            }
            if (cross & 1) buf[y * w + x] = 255;
        }
    }
}

static SvgArcRow g_log[SVG_MAX_ARCS];
static int g_log_count = 0;

int svg_arc_log_count(void) { return g_log_count; }
const SvgArcRow *svg_arc_log(void) { return g_log; }

int svg_fill_ex(const VPath *p, int w, int h, unsigned char *buf,
                unsigned arc_mode, double tol, SvgFillStats *stats) {
    if (!p || !buf || w <= 0 || h <= 0) {
        err_set(SVG_E_ARG, -1, "buf/w/h", NAN, "invalid fill arguments");
        return SVG_E_ARG;
    }
    if (arc_mode != SVG_ARC_CIRCLE && arc_mode != SVG_ARC_ELLIPSE
        && arc_mode != SVG_ARC_ELLIPSE_MIN) {
        err_set(SVG_E_FLAGS, -1, "arc_mode", (double)arc_mode, "unknown arc mode");
        return SVG_E_FLAGS;
    }
    if (!isfinite(tol) || tol <= 0.0) tol = SVG_DEFAULT_TOL;

    memset(buf, 0, (size_t)w * (size_t)h);

    FillCtx c;
    c.cap = 1024;   /* 初始容量；push_edge 按需翻倍到 SVG_EDGE_CAP */
    c.edges = (Edge *)malloc(sizeof(Edge) * (size_t)c.cap);
    if (!c.edges) {
        err_set(SVG_E_ARG, -1, "", NAN, "out of memory");
        return SVG_E_ARG;
    }
    c.count = 0;
    c.buf = buf;
    c.w = w;
    c.h = h;

    g_log_count = 0;
    SvgFillStats st;
    memset(&st, 0, sizeof st);
    st.tol = tol;
    st.whole = (arc_mode == SVG_ARC_CIRCLE);
    int rc = SVG_OK;

    for (int i = 0; i < p->count && rc == SVG_OK; ++i) {
        const VSeg *s = &p->segs[i];
        if (s->kind == SV_ARC) {
            if (st.arcs >= SVG_MAX_ARCS) {
                err_set(SVG_E_CAP, i, "arcs", (double)SVG_MAX_ARCS, "too many arcs");
                rc = SVG_E_CAP;
                break;
            }
            SvgArcRow row;
            memset(&row, 0, sizeof row);
            double b = 0.0;
            if (arc_mode == SVG_ARC_CIRCLE)
                rc = push_circle(&c, i, s, tol, &row, &b);
            else
                rc = push_ellipse(&c, i, s, tol, arc_mode == SVG_ARC_ELLIPSE_MIN, &row, &b);
            if (rc == SVG_OK) {
                g_log[g_log_count++] = row;
                ++st.arcs;
                st.nplus += row.nplus;
                st.nminus += row.nminus;
                if (b > st.max_bound) st.max_bound = b;
            }
        } else {
            rc = push_edge(&c, s->a, s->b);
            if (rc != SVG_OK)
                err_set(rc, i, "edges", (double)c.cap, "edge capacity exceeded");
        }
    }

    if (rc == SVG_OK) {
        fill_scan(&c);
        st.chords = c.count;
    }
    free(c.edges);
    if (stats) *stats = st;
    if (rc == SVG_OK) err_clear(SVG_OK);
    return rc;
}

int svg_fill(const VPath *p, int w, int h, unsigned char *buf) {
    return svg_fill_ex(p, w, h, buf, SVG_ARC_CIRCLE, SVG_DEFAULT_TOL, 0);
}

int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h) {
    if (!path || !buf || w <= 0 || h <= 0) {
        err_set(SVG_E_ARG, -1, "path/buf/w/h", NAN, "invalid PGM arguments");
        return SVG_E_ARG;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        err_set(SVG_E_IO, -1, "path", NAN, "cannot open output file");
        return SVG_E_IO;
    }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    return 0;
}
