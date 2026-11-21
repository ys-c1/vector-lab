#include "stroke.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------- 旧接口：逐点行为与历史版本完全一致 ---------- */
static int legacy_engine(const SPoint *pts, int n, double width, SPoint *out, int cap) {
    double h = width * 0.5;
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) {
        double dx = 0.0, dy = 0.0;
        if (i + 1 < n) { dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y; }
        else { dx = pts[i].x - pts[i-1].x; dy = pts[i].y - pts[i-1].y; }
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) return -2;
        double nx = -dy / len * h, ny = dx / len * h;
        out[k].x = pts[i].x + nx; out[k].y = pts[i].y + ny; ++k;
        out[k].x = pts[i].x - nx; out[k].y = pts[i].y - ny; ++k;
    }
    return k;
}

int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out) {
    if (!pts || !out || n < 2 || width <= 0.0) return -1;
    int k = legacy_engine(pts, n, width, out->pts, 126);
    if (k < 0) return k;
    out->count = k;
    return 0;
}

/* ---------- 几何辅助 ---------- */
static double clamp1(double v) { return v < -1.0 ? -1.0 : (v > 1.0 ? 1.0 : v); }

/* 偏移直线 q1+u*s 与 q2+v*t 的交点，方向向量须为单位/任意非平行向量 */
static SPoint line_meet(SPoint q1, SPoint u, SPoint q2, SPoint v) {
    double det = u.x * v.y - u.y * v.x;
    double s = ((q2.x - q1.x) * v.y - (q2.y - q1.y) * v.x) / det;
    SPoint r = { q1.x + u.x * s, q1.y + u.y * s };
    return r;
}

typedef struct { double tx, ty, lx, ly; } Tangent; /* 单位切向与左法向 */

static int push_pt(SOutline *o, SPoint p) {
    if (o->count >= STROKE_MAX_PTS) return -5;
    o->pts[o->count++] = p;
    return 0;
}

/* 在指定侧（side=+1 左, -1 右）沿路径顺序发射一条偏移链 */
static int emit_side(SOutline *o, const SPoint *pts, const Tangent *tg, int n,
                     double h, int side, const SStrokeOpt *opt) {
    /* 起点端盖（butt）偏移点 */
    SPoint start = { pts[0].x + side * tg[0].lx * h,
                     pts[0].y + side * tg[0].ly * h };
    if (push_pt(o, start)) return -5;

    for (int i = 1; i < n - 1; ++i) {
        const Tangent *a = &tg[i - 1], *b = &tg[i];
        double cr = a->tx * b->ty - a->ty * b->tx;
        int left_turn = cr > 0.0;
        /* 外侧：左拐外扩在左法向侧(side +1)，右拐在右法向侧(side -1) */
        int is_outer = (side > 0) == left_turn;
        double nax = side * a->lx, nay = side * a->ly;
        double nbx = side * b->lx, nby = side * b->ly;
        SPoint u = { a->tx, a->ty }, v = { b->tx, b->ty };
        SPoint q1 = { pts[i].x + nax * h, pts[i].y + nay * h };
        SPoint q2 = { pts[i].x + nbx * h, pts[i].y + nby * h };

        if (fabs(cr) <= SJ_EPS) {
            /* 直行：偏移线共线，若新点在末边的延长线上则跳过（共线拐点不增点） */
            int skip = 0;
            if (o->count >= 2) {
                SPoint *p0 = &o->pts[o->count - 2], *p1 = &o->pts[o->count - 1];
                double ex = p1->x - p0->x, ey = p1->y - p0->y;
                double fx = q1.x - p1->x, fy = q1.y - p1->y;
                double el = hypot(ex, ey), fl = hypot(fx, fy);
                if (el > SJ_EPS && fl > SJ_EPS &&
                    fabs(ex * fy - ey * fx) / (el * fl) < 1e-12 &&
                    (ex * fx + ey * fy) > 0.0)
                    skip = 1;
            }
            if (!skip && push_pt(o, q1)) return -5;
            continue;
        }

        if (!is_outer || opt->join == SJ_MITER ||
            (opt->join == SJ_AUTO && opt->info[i - 1].join == SJ_MITER)) {
            /* 内侧永远是两偏移线交点；外侧斜接同理 */
            SPoint m = line_meet(q1, u, q2, v);
            if (push_pt(o, m)) return -5;
        } else if (opt->join == SJ_BEVEL ||
                   (opt->join == SJ_AUTO && opt->info[i - 1].join == SJ_BEVEL)) {
            if (push_pt(o, q1)) return -5; /* 入边外侧偏移点 */
            if (push_pt(o, q2)) return -5; /* 出边外侧偏移点 */
        } else {
            /* 外弧：直接取 q1->q2 的极角短弧，角差归一化到 (-pi, pi] */
            double a0 = atan2(nay, nax);
            double a1 = atan2(nby, nbx);
            double sweep = a1 - a0;
            while (sweep >  M_PI) sweep -= 2.0 * M_PI;
            while (sweep <= -M_PI) sweep += 2.0 * M_PI;
            int kseg = (int)ceil(fabs(sweep) / SJ_ROUND_STEP);
            if (kseg < 1) kseg = 1;
            double step = sweep / kseg;
            if (push_pt(o, q1)) return -5; /* 弧起点：入边外侧偏移端点 */
            for (int k = 1; k <= kseg; ++k) {
                double ang = a0 + step * k;
                SPoint q = { pts[i].x + cos(ang) * h, pts[i].y + sin(ang) * h };
                if (push_pt(o, q)) return -5;
            }
        }
    }

    SPoint endp = { pts[n - 1].x + side * tg[n - 2].lx * h,
                    pts[n - 1].y + side * tg[n - 2].ly * h };
    int skip_end = 0;
    if (o->count >= 2) {
        SPoint *p0 = &o->pts[o->count - 2], *p1 = &o->pts[o->count - 1];
        double ex = p1->x - p0->x, ey = p1->y - p0->y;
        double fx = endp.x - p1->x, fy = endp.y - p1->y;
        double el = hypot(ex, ey), fl = hypot(fx, fy);
        if (el > SJ_EPS && fl > SJ_EPS &&
            fabs(ex * fy - ey * fx) / (el * fl) < 1e-12 &&
            (ex * fx + ey * fy) > 0.0)
            skip_end = 1;
    }
    if (!skip_end && push_pt(o, endp)) return -5;
    return 0;
}

int stroke_outline(const SPoint *pts, int n, double width,
                   SStrokeOpt *opt, SOutline *out, int *bad_index) {
    if (bad_index) *bad_index = -1;
    if (!pts || !out || n < 2 || n > STROKE_MAX_IN + 1 || width <= 0.0 || !opt)
        return -1;
    if (opt->join < SJ_AUTO || opt->join > SJ_LEGACY) return -1;
    double limit = opt->miter_limit > 0.0 ? opt->miter_limit : SJ_DEFAULT_MITER_LIMIT;
    out->count = 0;
    opt->record_count = 0;

    /* LEGACY：与旧接口同一引擎，逐点一致 */
    if (opt->join == SJ_LEGACY) {
        int k = legacy_engine(pts, n, width, out->pts, STROKE_MAX_PTS);
        if (k < 0) { if (bad_index) *bad_index = 0; return k; }
        out->count = k;
        return 0;
    }

    /* 第一遍：单位切向 / 左法向，拒绝零长度边 */
    Tangent tg[STROKE_MAX_IN];
    for (int s = 0; s < n - 1; ++s) {
        double dx = pts[s + 1].x - pts[s].x;
        double dy = pts[s + 1].y - pts[s].y;
        double len = sqrt(dx * dx + dy * dy);
        if (len < SJ_EPS) { if (bad_index) *bad_index = s; return -2; }
        tg[s].tx = dx / len; tg[s].ty = dy / len;
        tg[s].lx = -dy / len; tg[s].ly = dx / len;
    }

    /* 第二遍：逐拐点计算夹角、选择接法、记录并校验 */
    for (int i = 1; i < n - 1; ++i) {
        const Tangent *a = &tg[i - 1], *b = &tg[i];
        double cr = a->tx * b->ty - a->ty * b->tx;
        double dot = clamp1(a->tx * b->tx + a->ty * b->ty);
        double delta = acos(dot);           /* 偏转角 0..pi */
        double gamma = M_PI - delta;        /* 路径夹角 pi(直行)..0(折返) */
        double ratio = 1.0 / cos(delta * 0.5);
        int join = opt->join;
        int rejected = 0, reason = 0;

        if (fabs(cr) <= SJ_EPS && dot < 0.0) {
            rejected = 1; reason = -3;      /* 夹角为零（同向折返） */
        } else if (join == SJ_AUTO) {
            if (fabs(cr) <= SJ_EPS) join = SJ_MITER;           /* 直行 */
            else if (gamma >= SJ_MITER_ANGLE) join = SJ_MITER;
            else if (gamma <  SJ_ROUND_ANGLE) join = SJ_ROUND;
            else join = SJ_BEVEL;
        }
        if (!rejected && join == SJ_MITER && ratio > limit) {
            rejected = 1; reason = -4;      /* 斜接长度超限 */
        }

        SJoinInfo *rec = &opt->info[i - 1];
        rec->index = i;
        rec->angle = gamma;
        rec->join = join;
        rec->miter_ratio = ratio;
        rec->rejected = rejected;
        rec->reason = reason;
        if (rejected) { if (bad_index) *bad_index = i; return reason; }
    }
    opt->record_count = n - 2;

    /* 第三遍：左链正向 + 右链反向，组成闭合轮廓 */
    double h = width * 0.5;
    int rc = emit_side(out, pts, tg, n, h, +1, opt);
    if (rc) return rc;
    SOutline right;
    right.count = 0;
    rc = emit_side(&right, pts, tg, n, h, -1, opt);
    if (rc) return rc;
    for (int i = right.count - 1; i >= 0; --i)
        if (push_pt(out, right.pts[i])) return -5;
    return 0;
}
