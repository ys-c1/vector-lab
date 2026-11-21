#include "contact.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 先算的叉积：与 isect/src/isect.c 的 den 同式、同阈值。 */
#define CT_DEN_EPS   1e-12
/* 二次模型中 A 视为零的相对阈值。 */
#define CT_A_REL     1e-14
/* 判别式噪声带相对系数：band = 4|A|tol + CT_DISC_REL*(B^2 + |4AC|) */
#define CT_DISC_REL  1e-12
/* 实根是否落在区间内的容差（tau 单位）。 */
#define CT_ROOT_EPS  1e-12
/* 参数是否视为落在曲线/线段端点的容差。 */
#define CT_T_EPS     1e-9
#define CT_U_EPS     1e-9
/* 合并相切候选的窗口（区间宽度的倍数）。 */
#define CT_MERGE_W   1.05

static double cross2(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static CtPoint ct_eval(const CtPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3.0 * u * u * t, d = 3.0 * u * t * t, e = t * t * t;
    CtPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

static int pt_finite(CtPoint p) { return isfinite(p.x) && isfinite(p.y); }

static void set_field(char err_field[CT_FIELD_MAX], const char *name) {
    if (!err_field) return;
    if (!name) name = "";
    strncpy(err_field, name, CT_FIELD_MAX - 1);
    err_field[CT_FIELD_MAX - 1] = '\0';
}

const char *ct_kind_name(int kind) {
    switch (kind) {
    case CT_KIND_NONE:   return "none";
    case CT_KIND_CROSS:  return "cross";
    case CT_KIND_GRAZE:  return "graze";
    case CT_KIND_REJECT: return "reject";
    default:             return "?";
    }
}

const char *ct_reason_name(int reason) {
    switch (reason) {
    case CT_R_NONE:             return "";
    case CT_R_CROSS:            return "cross";
    case CT_R_TANGENT:          return "tangent";
    case CT_R_CURVE_ENDPOINT:   return "curve_endpoint";
    case CT_R_SEGMENT_ENDPOINT: return "segment_endpoint";
    case CT_R_COINCIDENT:       return "coincident";
    case CT_R_PARALLEL:         return "parallel";
    case CT_R_DISC_NEG:         return "discriminant_negative";
    case CT_R_U_OUTSIDE:        return "u_outside";
    case CT_R_ROOT_OUTSIDE:     return "root_outside_span";
    case CT_R_MODEL_MISS:       return "model_miss";
    case CT_R_LEGACY:           return "legacy";
    default:                    return "?";
    }
}

const char *ct_strerror(int code) {
    switch (code) {
    case CT_OK:      return "ok";
    case CT_E_ARG:   return "invalid argument: null pointer or index out of range";
    case CT_E_CTRL:  return "invalid control point: coordinate is not finite";
    case CT_E_SEG:   return "invalid segment: endpoint not finite or segment has zero length";
    case CT_E_TOL:   return "invalid argument: tol must be finite and >= 0";
    case CT_E_FLAGS: return "invalid argument: unknown flag bits";
    case CT_E_DISC:  return "rejected: discriminant negative in interval (field 'discriminant')";
    case CT_E_CAP:   return "capacity exceeded: too many contact points";
    case CT_E_IO:    return "csv write failed";
    default:         return "unknown error";
    }
}

static int validate(const CtPoint c[4], CtPoint b0, CtPoint b1,
                    int spans, unsigned flags, double tol,
                    char err_field[CT_FIELD_MAX]) {
    if (!c) { set_field(err_field, "c"); return CT_E_ARG; }
    if (spans < 1 || spans > CT_MAX_SPANS) { set_field(err_field, "spans"); return CT_E_ARG; }
    if (!isfinite(tol) || tol < 0.0) { set_field(err_field, "tol"); return CT_E_TOL; }
    if (flags & ~(unsigned)(CT_F_CLASSIFY | CT_F_STRICT_DISC)) {
        set_field(err_field, "flags");
        return CT_E_FLAGS;
    }
    for (int k = 0; k < 4; ++k) {
        if (!isfinite(c[k].x)) {
            char f[CT_FIELD_MAX];
            snprintf(f, sizeof f, "c[%d].x", k);
            set_field(err_field, f);
            return CT_E_CTRL;
        }
        if (!isfinite(c[k].y)) {
            char f[CT_FIELD_MAX];
            snprintf(f, sizeof f, "c[%d].y", k);
            set_field(err_field, f);
            return CT_E_CTRL;
        }
    }
    if (!pt_finite(b0)) { set_field(err_field, "b0"); return CT_E_SEG; }
    if (!pt_finite(b1)) { set_field(err_field, "b1"); return CT_E_SEG; }
    if (b0.x == b1.x && b0.y == b1.y) { set_field(err_field, "b0/b1"); return CT_E_SEG; }
    return CT_OK;
}

/* ------------------------------------------------------------------ *
 * 旧路径（不分类）：与现状一致的逐弦叉积求交。
 * ------------------------------------------------------------------ */
int ct_legacy_hits(const CtPoint c[4], CtPoint b0, CtPoint b1, int spans,
                   CtChordHit *hits, int cap, char err_field[CT_FIELD_MAX]) {
    set_field(err_field, "");
    if (!c) { set_field(err_field, "c"); return CT_E_ARG; }
    if (cap < 0 || (cap > 0 && !hits)) { set_field(err_field, "hits"); return CT_E_ARG; }
    int v = validate(c, b0, b1, spans, 0, 0.0, err_field);
    if (v != CT_OK) return v;

    CtPoint p[CT_MAX_SPANS + 1];
    for (int i = 0; i <= spans; ++i)
        p[i] = ct_eval(c, (double)i / (double)spans);

    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    int k = 0;
    for (int i = 0; i < spans; ++i) {
        double rx = p[i + 1].x - p[i].x, ry = p[i + 1].y - p[i].y;
        double den = cross2(rx, ry, sx, sy);
        if (fabs(den) < CT_DEN_EPS) continue;
        double qx = b0.x - p[i].x, qy = b0.y - p[i].y;
        double t = cross2(qx, qy, sx, sy) / den;
        double u = cross2(qx, qy, rx, ry) / den;
        if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) continue;
        if (hits && k < cap) {
            hits[k].chord = i;
            hits[k].t = t;
            hits[k].u = u;
            hits[k].at.x = p[i].x + rx * t;
            hits[k].at.y = p[i].y + ry * t;
        }
        ++k;
    }
    return k;
}

/* ------------------------------------------------------------------ *
 * 分类路径
 * ------------------------------------------------------------------ */

typedef struct {
    double sx, sy, L, L2;
    CtPoint b0;
} SegFrame;

/* 到线段所在直线的有符号距离（按线段长度归一化）。 */
static double h_of(const SegFrame *f, CtPoint p) {
    return cross2(f->sx, f->sy, p.x - f->b0.x, p.y - f->b0.y) / f->L;
}

/* 线段参数。 */
static double u_of(const SegFrame *f, CtPoint p) {
    return ((p.x - f->b0.x) * f->sx + (p.y - f->b0.y) * f->sy) / f->L2;
}

/* 二次模型 h(tau) = A tau^2 + B tau + C 与判别式 D = B^2 - 4AC。 */
typedef struct { double A, B, C, D, hv; } Quad;

static Quad quad_fit(double h0, double hm, double h1) {
    Quad q;
    q.A = 2.0 * (h0 - 2.0 * hm + h1);
    q.B = 4.0 * hm - 3.0 * h0 - h1;
    q.C = h0;
    q.D = q.B * q.B - 4.0 * q.A * q.C;
    q.hv = q.C - q.B * q.B / (4.0 * q.A); /* 模型极值（顶点）处的有符号距离 */
    return q;
}

static double clamp01(double x) {
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static void row_init(CtRow *r, int span) {
    memset(r, 0, sizeof *r);
    r->span = span;
    r->status = CT_KIND_NONE;
    r->reason = CT_R_NONE;
    r->t = NAN;
    r->u = NAN;
    r->at.x = NAN;
    r->at.y = NAN;
    r->resid = NAN;
    r->field = "";
}

/* 端点接触一律归入擦边。 */
static int endpoint_reason(double t, double u) {
    if (t <= CT_T_EPS || t >= 1.0 - CT_T_EPS) return CT_R_CURVE_ENDPOINT;
    if (u <= CT_U_EPS || u >= 1.0 - CT_U_EPS) return CT_R_SEGMENT_ENDPOINT;
    return CT_R_NONE;
}

/* 记录一个候选。u 越界返回 0（不记录）。 */
static int emit(const SegFrame *f, int span, CtHit hb[2], int *nhit, CtRow *row,
                int kind, double t, double u, CtPoint at, double disc,
                int reason) {
    if (u < -CT_U_EPS || u > 1.0 + CT_U_EPS) return 0;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    int ep = endpoint_reason(t, u);
    if (ep != CT_R_NONE && kind == CT_KIND_CROSS) { kind = CT_KIND_GRAZE; reason = ep; }
    else if (ep != CT_R_NONE && reason == CT_R_TANGENT) reason = ep;

    if (*nhit < 2) {
        CtHit *h = &hb[*nhit];
        h->kind = kind;
        h->span = span;
        h->t = t;
        h->u = u;
        h->at = at;
        h->disc = disc;
        h->resid = h_of(f, at);
        h->reason = reason;
        ++*nhit;
    }
    /* 行状态取该区间内“最显著”的候选：穿过优先于擦边。 */
    if (row->status == CT_KIND_NONE ||
        (kind == CT_KIND_CROSS && row->status == CT_KIND_GRAZE)) {
        row->status = kind;
        row->t = t;
        row->u = u;
        row->at = at;
        row->resid = h_of(f, at);
        row->reason = reason;
    }
    return 1;
}

/* 用根两侧的一小段判断曲线是否真的换边。 */
static int side_change(const CtPoint c[4], const SegFrame *f, double t,
                       double dt, double *hl, double *hr) {
    double delta = 0.25 * dt;
    double tl = t - delta, tr = t + delta;
    if (tl < 0.0) tl = 0.0;
    if (tr > 1.0) tr = 1.0;
    if (tl == t || tr == t) return 0;
    *hl = h_of(f, ct_eval(c, tl));
    *hr = h_of(f, ct_eval(c, tr));
    return (*hl < 0.0 && *hr > 0.0) || (*hl > 0.0 && *hr < 0.0);
}

/* 单个区间分类。返回写入 hb 的候选数；判别式为负时 row->status=REJECT、
 * row->field="discriminant"。 */
static int classify_span_impl(const CtPoint c[4], const SegFrame *f,
                              int spans, int span, double tol,
                              CtRow *row, CtHit hb[2]) {
    row_init(row, span);

    double ta = (double)span / (double)spans;
    double tb = (double)(span + 1) / (double)spans;
    double tm = 0.5 * (ta + tb);
    double dt = tb - ta;

    CtPoint p0 = ct_eval(c, ta), pm = ct_eval(c, tm), p1 = ct_eval(c, tb);
    double h0 = h_of(f, p0), hm = h_of(f, pm), h1 = h_of(f, p1);

    /* 第一步：两段方向叉积（区间弦 × 查询线段）。只作为粗筛记录；
     * 弦平行时不可能产生孤立交点，但曲线仍可能在中点附近擦线，
     * 所以是否接受由后面的判别式决定。 */
    double rx = p1.x - p0.x, ry = p1.y - p0.y;
    double chord_cross = cross2(rx, ry, f->sx, f->sy);
    row->chord_cross = chord_cross;
    row->chord_hit = 0;
    if (fabs(chord_cross) >= CT_DEN_EPS) {
        double qx = f->b0.x - p0.x, qy = f->b0.y - p0.y;
        double ct = cross2(qx, qy, f->sx, f->sy) / chord_cross;
        double cu = cross2(qx, qy, rx, ry) / chord_cross;
        row->chord_hit = (ct >= 0.0 && ct <= 1.0 && cu >= 0.0 && cu <= 1.0);
    }

    /* 第二步：三点有符号距离拟合二次式，算判别式。 */
    Quad q = quad_fit(h0, hm, h1);
    row->A = q.A; row->B = q.B; row->C = q.C; row->disc = q.D;
    row->h0 = h0; row->hm = hm; row->h1 = h1;

    double aeps = CT_A_REL * (fabs(q.B) + fabs(q.C) + 1e-300);
    double band = 4.0 * fabs(q.A) * tol +
                  CT_DISC_REL * (q.B * q.B + fabs(4.0 * q.A * q.C));
    int nhit = 0;

    /* 模型退化为线性/常数。 */
    if (fabs(q.A) <= aeps) {
        if (fabs(q.B) > aeps) {
            double r = -q.C / q.B;
            if (r >= -CT_ROOT_EPS && r <= 1.0 + CT_ROOT_EPS) {
                r = clamp01(r);
                double t = ta + r * dt;
                CtPoint at = ct_eval(c, t);
                double hl = 0.0, hr = 0.0;
                int sc = side_change(c, f, t, dt, &hl, &hr);
                if (!emit(f, span, hb, &nhit, row,
                          sc ? CT_KIND_CROSS : CT_KIND_GRAZE, t, u_of(f, at), at,
                          q.D, sc ? CT_R_CROSS : CT_R_TANGENT)) {
                    row->reason = CT_R_U_OUTSIDE;
                    row->field = "u";
                    row->t = t;
                    row->u = u_of(f, at);
                    row->at = at;
                }
            } else {
                row->reason = CT_R_ROOT_OUTSIDE;
            }
        } else if (fabs(q.C) <= tol) {
            /* 常数 h 且贴近线段：共线贴合。 */
            double u0 = u_of(f, p0), u1 = u_of(f, p1);
            double lo = fmax(0.0, fmin(u0, u1));
            double hi = fmin(1.0, fmax(u0, u1));
            if (lo <= hi) {
                double um = 0.5 * (lo + hi);
                double r = fabs(u1 - u0) > 1e-15 ? (um - u0) / (u1 - u0) : 0.5;
                r = clamp01(r);
                double t = ta + r * dt;
                CtPoint at = ct_eval(c, t);
                emit(f, span, hb, &nhit, row, CT_KIND_GRAZE, t, um, at, q.D,
                     CT_R_COINCIDENT);
            } else {
                row->reason = CT_R_PARALLEL;
            }
        } else {
            row->reason = CT_R_PARALLEL;
        }
        return nhit;
    }

    /* 第三步：判别式为负 -> 该区间无实根，拒绝并说明字段。 */
    if (q.D < -band) {
        double rv = clamp01(-q.B / (2.0 * q.A));
        double t = ta + rv * dt;
        CtPoint at = ct_eval(c, t);
        row->status = CT_KIND_REJECT;
        row->reason = CT_R_DISC_NEG;
        row->field = "discriminant";
        row->t = t;
        row->u = u_of(f, at);
        row->at = at;
        return 0;
    }

    /* 第四步：判别式近零（重根）或顶点落入容差带 -> 相切（擦边）。
     * |D| <= band 等价于 |h_vertex| <= tol。 */
    if (q.D <= band || fabs(q.hv) <= tol) {
        double rv = -q.B / (2.0 * q.A);
        if (rv >= -CT_ROOT_EPS && rv <= 1.0 + CT_ROOT_EPS) {
            rv = clamp01(rv);
            double t = ta + rv * dt;
            CtPoint at = ct_eval(c, t);
            double u = u_of(f, at);
            int reason = CT_R_TANGENT;
            int ep = endpoint_reason(t, u);
            if (ep != CT_R_NONE) reason = ep;
            if (!emit(f, span, hb, &nhit, row, CT_KIND_GRAZE, t, u, at, q.D, reason)) {
                row->reason = CT_R_U_OUTSIDE;
                row->field = "u";
                row->t = t;
                row->u = u;
                row->at = at;
            }
            return nhit;
        }
        /* 顶点在区间外：贴近端点时仍按擦边处理。 */
        double r0 = fabs(h0) <= fabs(h1) ? 0.0 : 1.0;
        double t = ta + r0 * dt;
        CtPoint at = ct_eval(c, t);
        double hh = h_of(f, at);
        if (fabs(hh) <= tol) {
            emit(f, span, hb, &nhit, row, CT_KIND_GRAZE, t, u_of(f, at), at, q.D,
                 CT_R_TANGENT);
        } else {
            row->reason = CT_R_ROOT_OUTSIDE;
            row->t = t;
            row->u = u_of(f, at);
            row->at = at;
        }
        return nhit;
    }

    /* 第五步：判别式为正、两个实根 -> 逐根分辨穿过与擦边。 */
    double sq = sqrt(q.D);
    double r1 = (-q.B - sq) / (2.0 * q.A);
    double r2 = (-q.B + sq) / (2.0 * q.A);
    if (r1 > r2) { double tmp = r1; r1 = r2; r2 = tmp; }
    int in1 = (r1 >= -CT_ROOT_EPS && r1 <= 1.0 + CT_ROOT_EPS);
    int in2 = (r2 >= -CT_ROOT_EPS && r2 <= 1.0 + CT_ROOT_EPS);
    if (!in1 && !in2) {
        double rv = clamp01(-q.B / (2.0 * q.A));
        double t = ta + rv * dt;
        CtPoint at = ct_eval(c, t);
        row->reason = CT_R_ROOT_OUTSIDE;
        row->t = t;
        row->u = u_of(f, at);
        row->at = at;
        return 0;
    }
    double rr[2];
    int nr = 0;
    if (in1) rr[nr++] = clamp01(r1);
    if (in2) rr[nr++] = clamp01(r2);
    for (int i = 0; i < nr; ++i) {
        double t = ta + rr[i] * dt;
        CtPoint at = ct_eval(c, t);
        double u = u_of(f, at);
        double hl = 0.0, hr = 0.0;
        int sc = side_change(c, f, t, dt, &hl, &hr);
        int kind = sc ? CT_KIND_CROSS : CT_KIND_GRAZE;
        int reason = sc ? CT_R_CROSS : CT_R_TANGENT;
        if (!emit(f, span, hb, &nhit, row, kind, t, u, at, q.D, reason)) {
            row->reason = CT_R_U_OUTSIDE;
            row->field = "u";
            if (row->status == CT_KIND_NONE) {
                row->t = t;
                row->u = u;
                row->at = at;
            }
        }
    }
    return nhit;
}

/* 合并重复候选：相邻区间的同一次擦边只留残差更小的一条；
 * 穿过点只在参数几乎重合（区间边界两侧）时去重。 */
static int merge_hits(CtScan *out, const CtHit *raw, int nraw) {
    int n = 0;
    double w = CT_MERGE_W / (double)out->spans;
    for (int k = 0; k < nraw; ++k) {
        CtHit h = raw[k];
        int skip = 0;
        for (int j = 0; j < n; ++j) {
            CtHit *p = &out->hits[j];
            if (h.kind == CT_KIND_GRAZE && p->kind == CT_KIND_GRAZE &&
                abs(p->span - h.span) <= 1 && fabs(p->t - h.t) <= w) {
                if (fabs(h.resid) < fabs(p->resid)) out->hits[j] = h;
                skip = 1;
                break;
            }
            if (h.kind == CT_KIND_CROSS && p->kind == CT_KIND_CROSS &&
                fabs(p->t - h.t) < 1e-9) {
                skip = 1;
                break;
            }
        }
        if (!skip) out->hits[n++] = h;
    }
    return n;
}

int ct_classify_span(const CtPoint c[4], CtPoint b0, CtPoint b1,
                     int spans, int span, double tol,
                     CtRow *out, char err_field[CT_FIELD_MAX]) {
    set_field(err_field, "");
    if (!out) { set_field(err_field, "out"); return CT_E_ARG; }
    if (span < 0 || span >= spans) { set_field(err_field, "span"); return CT_E_ARG; }
    int v = validate(c, b0, b1, spans, CT_F_CLASSIFY, tol, err_field);
    if (v != CT_OK) return v;

    SegFrame f;
    f.b0 = b0;
    f.sx = b1.x - b0.x; f.sy = b1.y - b0.y;
    f.L = hypot(f.sx, f.sy); f.L2 = f.L * f.L;
    CtHit hb[2];
    classify_span_impl(c, &f, spans, span, tol, out, hb);
    if (out->status == CT_KIND_REJECT) {
        set_field(err_field, "discriminant");
        return CT_E_DISC;
    }
    return CT_OK;
}

int ct_contact(const CtPoint c[4], CtPoint b0, CtPoint b1,
               int spans, unsigned flags, double tol,
               CtScan *out, char err_field[CT_FIELD_MAX]) {
    set_field(err_field, "");
    int v = validate(c, b0, b1, spans, flags, tol, err_field);
    if (v != CT_OK) return v;
    if (!out) return CT_OK;

    memset(out, 0, sizeof *out);

    /* 不分类：固定 8 段旧求交路径，忽略 spans，输出与现状逐位一致。 */
    if (!(flags & CT_F_CLASSIFY)) {
        CtChordHit lh[CT_MAX_HITS];
        int count = ct_legacy_hits(c, b0, b1, CT_LEGACY_SPANS, lh, CT_MAX_HITS,
                                   err_field);
        if (count < 0) return count;
        if (count > CT_MAX_HITS) { set_field(err_field, "hits"); return CT_E_CAP; }
        out->spans = CT_LEGACY_SPANS;
        out->classified = 0;
        out->nrows = 0;
        out->raw = count;
        out->count = count;
        out->merged = 0;
        for (int i = 0; i < count; ++i) {
            CtHit *h = &out->hits[i];
            memset(h, 0, sizeof *h);
            h->kind = CT_KIND_CROSS;
            h->span = lh[i].chord;
            h->t = ((double)lh[i].chord + lh[i].t) / (double)CT_LEGACY_SPANS;
            h->u = lh[i].u;
            h->at = lh[i].at;
            h->disc = NAN;
            h->resid = 0.0;
            h->reason = CT_R_LEGACY;
            ++out->crosses;
        }
        return CT_OK;
    }

    SegFrame f;
    f.b0 = b0;
    f.sx = b1.x - b0.x; f.sy = b1.y - b0.y;
    f.L = hypot(f.sx, f.sy); f.L2 = f.L * f.L;

    CtHit raw[CT_MAX_HITS];
    int nraw = 0;
    out->spans = spans;
    out->classified = 1;
    out->nrows = spans;
    for (int i = 0; i < spans; ++i) {
        CtHit hb[2];
        int nhit = classify_span_impl(c, &f, spans, i, tol, &out->rows[i], hb);
        for (int k = 0; k < nhit; ++k) {
            if (nraw >= CT_MAX_HITS) { set_field(err_field, "hits"); return CT_E_CAP; }
            raw[nraw++] = hb[k];
        }
        if (out->rows[i].status == CT_KIND_REJECT) {
            ++out->rejects;
            if (out->rows[i].reason == CT_R_DISC_NEG) ++out->disc_neg;
        }
    }
    out->raw = nraw;
    out->count = merge_hits(out, raw, nraw);
    out->merged = nraw - out->count;
    for (int i = 0; i < out->count; ++i) {
        if (out->hits[i].kind == CT_KIND_CROSS) ++out->crosses;
        else if (out->hits[i].kind == CT_KIND_GRAZE) ++out->grazes;
    }

    if ((flags & CT_F_STRICT_DISC) && out->disc_neg > 0) {
        set_field(err_field, "discriminant");
        return CT_E_DISC;
    }
    return CT_OK;
}

/* ------------------------------------------------------------------ *
 * CSV 导出：NaN 写为空字段。
 * ------------------------------------------------------------------ */
static void num_field(FILE *fp, double v) {
    if (!isnan(v)) fprintf(fp, "%.17g", v);
}

static int csv_ok(FILE *fp) { return ferror(fp) ? CT_E_IO : CT_OK; }

int ct_export_rows(const CtScan *scan, FILE *fp) {
    if (!scan || !fp) return CT_E_ARG;
    fputs("spans,classified,span,status,chord_cross,chord_hit,A,B,C,"
          "discriminant,h0,hm,h1,t,u,x,y,resid,reason,field\n", fp);
    for (int i = 0; i < scan->nrows; ++i) {
        const CtRow *r = &scan->rows[i];
        fprintf(fp, "%d,%d,%d,%s,", scan->spans, scan->classified, r->span,
                ct_kind_name(r->status));
        num_field(fp, r->chord_cross); fprintf(fp, ",%d,", r->chord_hit);
        num_field(fp, r->A); fputc(',', fp);
        num_field(fp, r->B); fputc(',', fp);
        num_field(fp, r->C); fputc(',', fp);
        num_field(fp, r->disc); fputc(',', fp);
        num_field(fp, r->h0); fputc(',', fp);
        num_field(fp, r->hm); fputc(',', fp);
        num_field(fp, r->h1); fputc(',', fp);
        num_field(fp, r->t); fputc(',', fp);
        num_field(fp, r->u); fputc(',', fp);
        num_field(fp, r->at.x); fputc(',', fp);
        num_field(fp, r->at.y); fputc(',', fp);
        num_field(fp, r->resid); fputc(',', fp);
        fprintf(fp, "%s,%s\n", ct_reason_name(r->reason),
                r->field ? r->field : "");
    }
    return csv_ok(fp);
}

int ct_export_hits(const CtScan *scan, FILE *fp) {
    if (!scan || !fp) return CT_E_ARG;
    fputs("kind,span,t,u,x,y,discriminant,resid,reason\n", fp);
    for (int i = 0; i < scan->count; ++i) {
        const CtHit *h = &scan->hits[i];
        fprintf(fp, "%s,%d,", ct_kind_name(h->kind), h->span);
        num_field(fp, h->t); fputc(',', fp);
        num_field(fp, h->u); fputc(',', fp);
        num_field(fp, h->at.x); fputc(',', fp);
        num_field(fp, h->at.y); fputc(',', fp);
        num_field(fp, h->disc); fputc(',', fp);
        num_field(fp, h->resid); fputc(',', fp);
        fprintf(fp, "%s\n", ct_reason_name(h->reason));
    }
    return csv_ok(fp);
}
