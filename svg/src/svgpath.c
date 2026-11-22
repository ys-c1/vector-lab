#include "svgpath.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* 旧解析器：行为保持原样（包括多子路径时 start 不重置的历史行为）。   */
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

/* ------------------------------------------------------------------ */
/* 子路径解析：每个 M/m 开启一条子路径，Z/z 补一条闭合边。            */
/* ------------------------------------------------------------------ */

/* 退化（LEGACY）解析：逐式复刻 svg_parse，连“多子路径时 start 不重置”
 * 的历史行为也原样保留，保证旧输出逐字节一致。
 * 子路径按 M/m 切分；Z/z 只给当前子路径补一条 (cur -> start) 的闭合边，
 * 与旧解析器完全一样地可能跨子路径取 start。 */
int svg_parse_legacy(const char *d, VSubPaths *out) {
    VPath *vp = &out->path;
    out->sub_count = 0;
    out->bad_sub = 0;
    vp->count = 0;
    const char *p = skip_sep(d);
    if (*p != 'M' && *p != 'm') return SVG_E_NOMOVE;
    VPoint cur = {0, 0}, start = {0, 0};
    int first = 1, first_of_sub = 0;
    while (*p) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        if (cmd != 'M' && cmd != 'm' && cmd != 'L' && cmd != 'l'
            && cmd != 'Z' && cmd != 'z') return SVG_E_CMD;
        ++p;
        if (cmd == 'Z' || cmd == 'z') {
            if (out->sub_count == 0) return SVG_E_NOMOVE;
            if (vp->count < 255) {
                VSeg s = {SV_LINE, cur, start};
                vp->segs[vp->count++] = s;
                ++out->subs[out->sub_count - 1].count;
            }
            out->subs[out->sub_count - 1].closed = 1;
            cur = start;
            first_of_sub = 0;
            continue;
        }
        double x = 0, y = 0;
        p = skip_sep(p);
        p = read_num(p, &x);
        if (!p) return SVG_E_NUM;
        p = skip_sep(p);
        p = read_num(p, &y);
        if (!p) return SVG_E_NUM;
        if (cmd == 'm' || cmd == 'l') { x += cur.x; y += cur.y; }
        if (first) { start.x = x; start.y = y; first = 0; }
        if (cmd == 'M' || cmd == 'm') {
            if (!first_of_sub) {
                if (out->sub_count >= SVG_MAX_SUBPATHS) return SVG_E_CAP;
                VSubPath *sub = &out->subs[out->sub_count++];
                sub->first = vp->count;
                sub->count = 0;
                sub->closed = 0;
                first_of_sub = 1;
            }
            cur.x = x; cur.y = y;
            continue;
        }
        /* L / l */
        if (vp->count < 255) {
            VSeg s = {SV_LINE, cur, {x, y}};
            vp->segs[vp->count++] = s;
            ++out->subs[out->sub_count - 1].count;
        }
        cur.x = x; cur.y = y;
        if (vp->count >= 255) return SVG_E_CAP;
    }
    if (out->sub_count == 0) return SVG_E_EMPTY;
    if (!out->subs[out->sub_count - 1].closed) {
        out->bad_sub = out->sub_count;
        return SVG_E_UNCLOSED;
    }
    return SVG_OK;
}

int svg_parse_ex(const char *d, VSubPaths *out) {
    if (!d || !out) return SVG_E_ARG;
    out->path.count = 0;
    out->sub_count = 0;
    out->bad_sub = 0;
    const char *p = skip_sep(d);
    if (!*p) return SVG_E_EMPTY;
    if (*p != 'M' && *p != 'm') return SVG_E_NOMOVE;

    VPoint cur = {0, 0}, start = {0, 0};
    int have_sub = 0;
    for (;;) {
        p = skip_sep(p);
        if (!*p) break;
        char cmd = *p;
        if (cmd != 'M' && cmd != 'm' && cmd != 'L' && cmd != 'l'
            && cmd != 'Z' && cmd != 'z') return SVG_E_CMD;
        ++p;

        if (cmd == 'Z' || cmd == 'z') {
            if (!have_sub) return SVG_E_NOMOVE;
            VSubPath *sub = &out->subs[out->sub_count - 1];
            if (!sub->closed) {
                if (out->path.count >= SVG_MAX_SEGS) return SVG_E_CAP;
                VSeg s = {SV_LINE, cur, start};
                out->path.segs[out->path.count++] = s;
                ++sub->count;
                sub->closed = 1;
            }
            cur = start;
            continue;
        }

        double x = 0, y = 0;
        p = skip_sep(p);
        p = read_num(p, &x);
        if (!p) return SVG_E_NUM;
        p = skip_sep(p);
        p = read_num(p, &y);
        if (!p) return SVG_E_NUM;
        if (cmd == 'm' || cmd == 'l') { x += cur.x; y += cur.y; }

        if (cmd == 'M' || cmd == 'm') {
            /* 上一条子路径必须先闭合，否则报出它的子路径号。 */
            if (have_sub && !out->subs[out->sub_count - 1].closed) {
                out->bad_sub = out->sub_count;   /* 1 起 */
                return SVG_E_UNCLOSED;
            }
            if (out->sub_count >= SVG_MAX_SUBPATHS) return SVG_E_CAP;
            VSubPath *sub = &out->subs[out->sub_count++];
            sub->first = out->path.count;
            sub->count = 0;
            sub->closed = 0;
            have_sub = 1;
            start.x = x; start.y = y;
            cur.x = x; cur.y = y;
            continue;
        }

        /* L / l */
        if (out->path.count >= SVG_MAX_SEGS) return SVG_E_CAP;
        VSeg s = {SV_LINE, cur, {x, y}};
        out->path.segs[out->path.count++] = s;
        ++out->subs[out->sub_count - 1].count;
        cur.x = x; cur.y = y;
    }

    if (out->sub_count == 0) return SVG_E_EMPTY;
    if (!out->subs[out->sub_count - 1].closed) {
        out->bad_sub = out->sub_count;
        return SVG_E_UNCLOSED;
    }
    return SVG_OK;
}

/* ------------------------------------------------------------------ */
/* 几何辅助                                                            */
/* ------------------------------------------------------------------ */

/* 半开区间穿越测试，与旧 svg_fill 的算式相同：
 *   (y0 > sy) == (y1 > sy) 时跳过；否则按线性插值求交点 x。
 * 返回 1 表示穿越，*xc 为交点 x，*dir 为方向（+1 上、-1 下）。 */
static int crosses(const VSeg *s, double sy, double *xc, int *dir) {
    double y0 = s->a.y, y1 = s->b.y;
    if ((y0 > sy) == (y1 > sy)) return 0;
    double t = (sy - y0) / (y1 - y0);
    *xc = s->a.x + (s->b.x - s->a.x) * t;
    *dir = (y1 > y0) ? 1 : -1;
    return 1;
}

/* 在点 (sx,sy) 处按规则判断是否落墨；segs 可以是全部段或单条子路径。 */
static int rule_inside(const VSeg *segs, int n, double sx, double sy, int rule) {
    int count = 0, wind = 0;
    for (int i = 0; i < n; ++i) {
        double xc;
        int dir;
        if (!crosses(&segs[i], sy, &xc, &dir) || xc <= sx) continue;
        ++count;
        wind += dir;
    }
    if (rule == SVG_FILL_NONZERO) return wind != 0;
    return count & 1;
}

static double subpath_area(const VSubPaths *p, const VSubPath *sub) {
    double a = 0.0;
    for (int i = 0; i < sub->count; ++i) {
        const VSeg *s = &p->path.segs[sub->first + i];
        a += s->a.x * s->b.y - s->b.x * s->a.y;
    }
    return fabs(0.5 * a);
}

static int cmp_double(const void *pa, const void *pb) {
    double a = *(const double *)pa, b = *(const double *)pb;
    return a < b ? -1 : (a > b ? 1 : 0);
}

typedef struct { double x; int dir; } VCross;

static int cmp_cross(const void *pa, const void *pb) {
    const VCross *a = (const VCross *)pa, *b = (const VCross *)pb;
    if (a->x < b->x) return -1;
    if (a->x > b->x) return 1;
    return a->dir - b->dir;
}

/* 子路径是否存在最终落墨的内点：遍历自身顶点 y 分出的横条，
 * 取每对穿越的中线为候选，任一候选被最终规则判定落墨即返回 1。
 * 全部候选都不落墨，说明这条子路径围出的区域整体是洞。 */
static int subpath_has_fill(const VSubPaths *p, const VSubPath *sub,
                            int rule) {
    double ys[SVG_MAX_SEGS * 2];
    int ny = 0;
    for (int i = 0; i < sub->count; ++i) {
        const VSeg *s = &p->path.segs[sub->first + i];
        ys[ny++] = s->a.y;
        ys[ny++] = s->b.y;
    }
    qsort(ys, (size_t)ny, sizeof(double), cmp_double);
    for (int i = 0; i + 1 < ny; ++i) {
        double y0 = ys[i], y1 = ys[i + 1];
        if (y1 - y0 <= 1e-12) continue;
        double ym = 0.5 * (y0 + y1);
        /* 用全部段的穿越点切分横条，才能看到嵌套子路径切出的小块。 */
        double xs[SVG_MAX_SEGS];
        int nx = 0;
        for (int k = 0; k < p->path.count; ++k) {
            double xc; int dir;
            if (crosses(&p->path.segs[k], ym, &xc, &dir)) xs[nx++] = xc;
        }
        qsort(xs, (size_t)nx, sizeof(double), cmp_double);
        for (int k = 0; k + 1 < nx; ++k) {
            double px = 0.5 * (xs[k] + xs[k + 1]);
            /* 落点必须在这条子路径围出的区域内（只看自身段）。 */
            int self = 0;
            for (int q = 0; q < sub->count; ++q) {
                double xc; int dir;
                if (crosses(&p->path.segs[sub->first + q], ym, &xc, &dir) &&
                    xc > px) ++self;
            }
            if (!(self & 1)) continue;
            if (rule_inside(p->path.segs, p->path.count, px, ym, rule))
                return 1;
        }
    }
    return 0;
}

/* 洞统计：子路径内部点若最终不落墨，则这条子路径围出的区域是一个洞。
 * 同时填逐子路径明细 sub_area / sub_is_hole（下标为子路径号 - 1）。 */
static void collect_stats(const VSubPaths *p, int rule, SVGStats *st) {
    st->rule = rule;
    st->subpaths = p->sub_count;
    st->holes = 0;
    st->hole_sub = 0;
    st->hole_area = 0.0;
    int eval_rule = (rule == SVG_FILL_NONZERO) ? SVG_FILL_NONZERO
                                               : SVG_FILL_EVENODD;
    for (int j = 0; j < p->sub_count; ++j) {
        const VSubPath *sub = &p->subs[j];
        double area = subpath_area(p, sub);
        if (j < SVG_MAX_SUBPATHS) {
            st->sub_area[j] = area;
            st->sub_is_hole[j] = 0;
        }
        if (rule == SVG_FILL_LEGACY) continue;   /* 旧行为没有洞的概念 */
        if (area <= 0.0) continue;               /* 退化为零面积的子路径不算洞 */
        if (subpath_has_fill(p, sub, eval_rule)) continue;  /* 有落墨内点 */
        ++st->holes;
        if (st->hole_sub == 0) st->hole_sub = j + 1;
        st->hole_area += area;
        if (j < SVG_MAX_SUBPATHS) st->sub_is_hole[j] = 1;
    }
}

/* 校验子路径视图：未闭合时给出子路径号。 */
static int validate_subs(const VSubPaths *p) {
    if (!p || p->sub_count <= 0) return SVG_E_EMPTY;
    if (p->sub_count > SVG_MAX_SUBPATHS) return SVG_E_CAP;
    for (int i = 0; i < p->sub_count; ++i) {
        const VSubPath *s = &p->subs[i];
        if (s->first < 0 || s->count < 0 ||
            s->first + s->count > p->path.count) return SVG_E_ARG;
        if (!s->closed) return SVG_E_UNCLOSED;   /* bad_sub 由调用方填写 */
    }
    return SVG_OK;
}

/* ------------------------------------------------------------------ */
/* 填充                                                                */
/* ------------------------------------------------------------------ */

/* 旧填充：全段奇偶扫描线，输出保持不变。 */
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

int svg_fill_rule(const VSubPaths *p, int w, int h, unsigned char *buf,
                  int rule, SVGStats *stats) {
    if (stats) memset(stats, 0, sizeof *stats);
    if (!p || !buf || w <= 0 || h <= 0) return SVG_E_ARG;
    if (rule != SVG_FILL_LEGACY && rule != SVG_FILL_EVENODD &&
        rule != SVG_FILL_NONZERO) return SVG_E_RULE;

    int rc = validate_subs(p);
    if (rc != SVG_OK) {
        /* 首条未闭合子路径号（1 起）。 */
        int bad = p->bad_sub;
        if (bad == 0 && p->sub_count > 0) {
            for (int i = 0; i < p->sub_count; ++i) {
                if (!p->subs[i].closed) { bad = i + 1; break; }
            }
        }
        if (stats) {
            stats->rule = rule;
            stats->subpaths = p->sub_count;
            stats->bad_sub = bad;
        }
        return rc;
    }
    if (rule == SVG_FILL_LEGACY) {
        /* 退化路径：直接走旧扫描线，逐字节保持；子路径合法性仍先校验。 */
        memset(buf, 0, (size_t)w * (size_t)h);
        int r = svg_fill(&p->path, w, h, buf);
        if (r != 0) return r;
        if (stats) {
            collect_stats(p, rule, stats);
            stats->bad_sub = 0;
        }
        return SVG_OK;
    }

    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) {
        double sy = y + 0.5;
        for (int x = 0; x < w; ++x) {
            double sx = x + 0.5;
            int parity = 0, wind = 0;
            /* 按子路径分别采样穿越点。 */
            for (int j = 0; j < p->sub_count; ++j) {
                const VSubPath *sub = &p->subs[j];
                int count = 0;
                for (int i = 0; i < sub->count; ++i) {
                    double xc; int dir;
                    if (!crosses(&p->path.segs[sub->first + i], sy, &xc, &dir))
                        continue;
                    if (xc > sx) { ++count; wind += dir; }
                }
                parity ^= (count & 1);
            }
            int on = (rule == SVG_FILL_NONZERO) ? (wind != 0) : parity;
            if (on) buf[y * w + x] = 255;
        }
    }
    if (stats) {
        collect_stats(p, rule, stats);
        stats->bad_sub = 0;
    }
    return SVG_OK;
}

/* 两条线段的真交点（不含端点与共线重叠）：返回 1 并给出交点 y。 */
static int seg_meet_y(const VSeg *a, const VSeg *b, double *y) {
    double rx = a->b.x - a->a.x, ry = a->b.y - a->a.y;
    double sx = b->b.x - b->a.x, sy = b->b.y - b->a.y;
    double den = rx * sy - ry * sx;
    if (fabs(den) < 1e-15) return 0;
    double qx = b->a.x - a->a.x, qy = b->a.y - a->a.y;
    double t = (qx * sy - qy * sx) / den;
    double u = (qx * ry - qy * rx) / den;
    if (t <= 1e-12 || t >= 1.0 - 1e-12) return 0;
    if (u <= 1e-12 || u >= 1.0 - 1e-12) return 0;
    *y = a->a.y + ry * t;
    return 1;
}

/* 解析法面积：以顶点 y 与边-边交点的 y 为分条线（条内穿越点序不变），
 * 条内穿越点 x 随 y 线性，宽度也线性，中点宽度乘条高即为精确面积。 */
int svg_fill_area(const VSubPaths *p, int rule, double *area, SVGStats *stats) {
    if (stats) memset(stats, 0, sizeof *stats);
    if (!p || !area) return SVG_E_ARG;
    if (rule != SVG_FILL_EVENODD && rule != SVG_FILL_NONZERO)
        return SVG_E_RULE;

    int rc = validate_subs(p);
    if (rc != SVG_OK) {
        int bad = p->bad_sub;
        if (bad == 0 && p->sub_count > 0) {
            for (int i = 0; i < p->sub_count; ++i) {
                if (!p->subs[i].closed) { bad = i + 1; break; }
            }
        }
        if (stats) {
            stats->rule = rule;
            stats->subpaths = p->sub_count;
            stats->bad_sub = bad;
        }
        return rc;
    }

    double ys[SVG_MAX_SEGS * 2 + SVG_MAX_SEGS * SVG_MAX_SEGS / 4];
    int ny = 0;
    for (int i = 0; i < p->path.count; ++i) {
        ys[ny++] = p->path.segs[i].a.y;
        ys[ny++] = p->path.segs[i].b.y;
    }
    for (int i = 0; i < p->path.count && ny < (int)(sizeof ys / sizeof ys[0]); ++i) {
        for (int j = i + 1; j < p->path.count; ++j) {
            double y;
            if (!seg_meet_y(&p->path.segs[i], &p->path.segs[j], &y)) continue;
            if (ny < (int)(sizeof ys / sizeof ys[0])) ys[ny++] = y;
        }
    }
    qsort(ys, (size_t)ny, sizeof(double), cmp_double);

    double total = 0.0;
    for (int i = 0; i + 1 < ny; ++i) {
        double y0 = ys[i], y1 = ys[i + 1];
        double hgt = y1 - y0;
        if (hgt <= 1e-12) continue;
        double ym = 0.5 * (y0 + y1);
        VCross cr[SVG_MAX_SEGS];
        int nc = 0;
        for (int k = 0; k < p->path.count; ++k) {
            double xc; int dir;
            if (!crosses(&p->path.segs[k], ym, &xc, &dir)) continue;
            cr[nc].x = xc;
            cr[nc].dir = dir;
            ++nc;
        }
        qsort(cr, (size_t)nc, sizeof(VCross), cmp_cross);
        double width = 0.0;
        if (rule == SVG_FILL_EVENODD) {
            for (int k = 0; k + 1 < nc; k += 2)
                width += cr[k + 1].x - cr[k].x;
        } else {
            int wind = 0;
            for (int k = 0; k + 1 < nc; ++k) {
                wind += cr[k].dir;
                if (wind != 0) width += cr[k + 1].x - cr[k].x;
            }
        }
        total += width * hgt;
    }
    *area = total;
    if (stats) {
        collect_stats(p, rule, stats);
        stats->bad_sub = 0;
    }
    return SVG_OK;
}

const char *svg_strerror(int code) {
    switch (code) {
    case SVG_OK:         return "ok";
    case SVG_E_ARG:      return "invalid argument";
    case SVG_E_NOMOVE:   return "path must start with M/m";
    case SVG_E_CMD:      return "unsupported command (only M/m/L/l/Z/z)";
    case SVG_E_NUM:      return "missing coordinate";
    case SVG_E_CAP:      return "segment/subpath capacity exceeded";
    case SVG_E_EMPTY:    return "empty path";
    case SVG_E_UNCLOSED: return "subpath is not closed (bad_sub gives index)";
    case SVG_E_RULE:     return "unknown fill rule";
    default:             return "unknown error";
    }
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
