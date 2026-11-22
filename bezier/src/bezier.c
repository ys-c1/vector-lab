#include "bezier.h"
#include <math.h>
#include <stdio.h>

#define BEZ_DEPTH_MAX 48

BPoint bezier_eval(const BPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    BPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

static double dist(BPoint a, BPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

/*
 * 旧接口：固定 8 段、与 tol 无关。保留原样，输出必须逐字节不变。
 */
int bezier_flatten(const BPoint c[4], double tol, BPoly *out) {
    if (!out || tol <= 0.0) return -1;
    int n = 8;
    if (n > 63) n = 63;
    out->count = n + 1;
    for (int i = 0; i <= n; ++i) out->pts[i] = bezier_eval(c, (double)i / (double)n);
    (void)dist;
    return 0;
}

double bezier_chord(const BPoint c[4]) { return dist(c[0], c[3]); }

const char *bezier_strerror(int code) {
    switch (code) {
    case BEZ_OK:           return "ok";
    case BEZ_E_ARG:        return "invalid argument (null pointer or out-of-range count)";
    case BEZ_E_TOL:        return "invalid tolerance: must be positive and finite (parameter: tolerance)";
    case BEZ_E_DEGEN:      return "degenerate control points";
    case BEZ_E_CAPACITY:   return "output exceeds BEZ_MAX_PIECES pieces";
    case BEZ_E_DEPTH:      return "subdivision depth exhausted (ill-conditioned segment)";
    default:               return "unknown error";
    }
}

/* ------------------------------------------------------------------ */
/* 误差上界与容差反解                                                  */
/* ------------------------------------------------------------------ */

/*
 * 一段三次贝塞尔的二阶差分峰值：
 *   m = max(|P0 - 2P1 + P2|, |P1 - 2P2 + P3|)
 * 该段曲线到其端点弦的最大距离不超过 (3/4)*m（见头文件推导与
 * tools/verify_bound.py 的独立随机验证）。
 */
static double second_diff(const BPoint c[4]) {
    double ax = c[0].x - 2.0 * c[1].x + c[2].x;
    double ay = c[0].y - 2.0 * c[1].y + c[2].y;
    double bx = c[1].x - 2.0 * c[2].x + c[3].x;
    double by = c[1].y - 2.0 * c[2].y + c[3].y;
    double ma = sqrt(ax * ax + ay * ay);
    double mb = sqrt(bx * bx + by * by);
    return ma > mb ? ma : mb;
}

double bezier_tol_for_error(double err_bound) {
    if (!isfinite(err_bound) || err_bound <= 0.0) return -1.0;
    double tol = err_bound / BEZ_CHORD_BOUND;   /* T = E / (3/4) */
    if (!isfinite(tol) || tol <= 0.0) return -1.0;
    /* 向下收敛到 1 ulp，使 (3/4)*T <= E 在浮点下也成立：
     * 否则恰好落在边界上的段可能因舍入略超上界。 */
    double prod = BEZ_CHORD_BOUND * tol;
    if (prod > err_bound) tol = nextafter(tol, 0.0);
    return tol;
}

/* ------------------------------------------------------------------ */
/* 单段细分                                                            */
/* ------------------------------------------------------------------ */

static int finite_pt(BPoint p) { return isfinite(p.x) && isfinite(p.y); }

static int seg_degenerate(const BPoint c[4]) {
    for (int i = 0; i < 4; ++i)
        if (!finite_pt(c[i])) return 1;
    /* 控制多边形收缩成一点：整段零长，下游描边/弧长无法处理。 */
    if (c[0].x == c[1].x && c[0].y == c[1].y &&
        c[0].x == c[2].x && c[0].y == c[2].y &&
        c[0].x == c[3].x && c[0].y == c[3].y)
        return 1;
    return 0;
}

/* 点到线段的距离（也是点到闭集合的距离函数）。 */
static double dist_point_seg(BPoint p, BPoint a, BPoint b) {
    double vx = b.x - a.x, vy = b.y - a.y;
    double L2 = vx * vx + vy * vy;
    double s = 0.0;
    if (L2 > 0.0) {
        s = ((p.x - a.x) * vx + (p.y - a.y) * vy) / L2;
        if (s < 0.0) s = 0.0;
        if (s > 1.0) s = 1.0;
    }
    double qx = a.x + s * vx, qy = a.y + s * vy;
    double dx = p.x - qx, dy = p.y - qy;
    return sqrt(dx * dx + dy * dy);
}

/* 单段是否已足够平：三个充分判据，任一成立即保证整段曲线到线段的
 * 最大距离 <= E。曲线落在控制点凸包内，所以判据 1、3 对凸包逐点成立。
 *   1) 所有控制点到弦线垂距 <= E，且投影都在弦内：凸包每点到线段的
 *      距离 <= E（投影是线性泛函，凸组合后仍在弦内）。
 *   2) 二阶差分峰值 m <= T = 4E/3，且投影都在弦内：曲线点投影仍在
 *      弦内，故到线段距离 = 到弦线距离 <= (3/4)m <= E。
 * 投影跑出弦时 (3/4)m 只界住到“直线”的距离，界不住到“线段”的距离，
 * 这时改用判据 3。
 *   3) 每个控制点到线段的距离 <= E：距离函数对凸集是凸的，凸包上的
 *      最大值在顶点取得，故曲线到线段距离 <= E。弦退化为一点时同样成立。 */
static int seg_is_flat(const BPoint c[4], double tol) {
    /* 容差 tol 对应的弦高误差界为 (3/4)*tol（反解已保证不超过 E）。 */
    double lim = BEZ_CHORD_BOUND * tol * (1.0 - 1e-12);
    BPoint p0 = c[0], p3 = c[3];
    double dx = p3.x - p0.x, dy = p3.y - p0.y;
    double L = sqrt(dx * dx + dy * dy);
    if (L > 0.0) {
        double ux = dx / L, uy = dy / L;
        double q1 = (c[1].x - p0.x) * ux + (c[1].y - p0.y) * uy;
        double q2 = (c[2].x - p0.x) * ux + (c[2].y - p0.y) * uy;
        double eps = 1e-12 * (1.0 + L);
        if (q1 >= -eps && q1 <= L + eps && q2 >= -eps && q2 <= L + eps) {
            double d1 = fabs((c[1].x - p0.x) * uy - (c[1].y - p0.y) * ux);
            double d2 = fabs((c[2].x - p0.x) * uy - (c[2].y - p0.y) * ux);
            if (d1 <= lim && d2 <= lim) return 1;          /* 判据 1 */
            if (second_diff(c) <= tol) return 1;           /* 判据 2 */
        }
    }
    double worst = 0.0;
    for (int k = 0; k < 4; ++k) {
        double d = dist_point_seg(c[k], p0, p3);
        if (d > worst) worst = d;
    }
    return worst <= lim;                                   /* 判据 3 */
}

static void split3(const BPoint c[4], BPoint left[4], BPoint right[4]) {
    BPoint a = { 0.5 * (c[0].x + c[1].x), 0.5 * (c[0].y + c[1].y) };
    BPoint b = { 0.5 * (c[1].x + c[2].x), 0.5 * (c[1].y + c[2].y) };
    BPoint d = { 0.5 * (c[2].x + c[3].x), 0.5 * (c[2].y + c[3].y) };
    BPoint e = { 0.5 * (a.x + b.x), 0.5 * (a.y + b.y) };
    BPoint f = { 0.5 * (b.x + d.x), 0.5 * (b.y + d.y) };
    BPoint g = { 0.5 * (e.x + f.x), 0.5 * (e.y + f.y) };
    left[0] = c[0]; left[1] = a; left[2] = e; left[3] = g;
    right[0] = g; right[1] = f; right[2] = d; right[3] = c[3];
}

typedef struct {
    BFlat *out;
    int seg_base;      /* 该输入段之前已输出的折线段数 */
    int src;           /* 输入段号 */
    double tol;
    int rc;            /* 首个错误码，0 表示尚无错误 */
    int bad_seg;       /* 错误所在输入段号 */
} SubCtx;

/* 返回写出的折线段数；出错时置 ctx->rc / bad_seg 并返回已写段数。 */
static int subdivide_seg(const BPoint c[4], double t0, double t1, int depth,
                         SubCtx *ctx) {
    if (ctx->rc != BEZ_OK) return 0;

    if (seg_is_flat(c, ctx->tol)) {
        if (ctx->out->pieces >= BEZ_MAX_PIECES) {
            ctx->rc = BEZ_E_CAPACITY;
            ctx->bad_seg = ctx->src;
            return 0;
        }
        BPoint q = c[3];
        int i = ctx->out->pieces;
        ctx->out->pts[i] = c[0];
        ctx->out->pts[i + 1] = q;
        ctx->out->t0[i] = t0;
        ctx->out->t1[i] = t1;
        ctx->out->seg[i] = ctx->src;
        ctx->out->pieces = i + 1;
        return 1;
    }

    if (depth >= BEZ_DEPTH_MAX) {
        ctx->rc = BEZ_E_DEPTH;
        ctx->bad_seg = ctx->src;
        return 0;
    }

    BPoint l[4], r[4];
    split3(c, l, r);
    double tm = 0.5 * (t0 + t1);
    int nl = subdivide_seg(l, t0, tm, depth + 1, ctx);
    if (ctx->rc != BEZ_OK) return nl;
    int nr = subdivide_seg(r, tm, t1, depth + 1, ctx);
    return nl + nr;
}

/* ------------------------------------------------------------------ */
/* 路径入口                                                            */
/* ------------------------------------------------------------------ */

static void result_reset(BPathResult *res) {
    if (!res) return;
    res->nsegs = 0;
    res->pieces = 0;
    res->err_bound = 0.0;
    res->tol = 0.0;
    res->max_err = 0.0;
    res->pass = 0;
    res->violations = 0;
    res->bad_seg = -1;
    res->worst_seg = -1;
    res->worst_piece = -1;
    res->worst_piece_in_seg = -1;
}

/* 校验与细分共用；返回 0 或负错误码，出错时填 res->bad_seg。 */
static int flatten_path_impl(const BPoint (*segs)[4], int nsegs,
                             double err_bound, double tol,
                             BFlat *out, BPathResult *res) {
    if (!segs || !out || nsegs < 1 || nsegs > BEZ_MAX_PATH_SEGS) {
        if (res) res->bad_seg = -1;
        return BEZ_E_ARG;
    }
    out->pieces = 0;
    if (res) {
        res->nsegs = nsegs;
        res->tol = tol;
        res->err_bound = err_bound;
    }
    for (int s = 0; s < nsegs; ++s) {
        if (seg_degenerate(segs[s])) {
            if (res) res->bad_seg = s;
            return BEZ_E_DEGEN;
        }
    }
    SubCtx ctx = { out, 0, 0, tol, BEZ_OK, -1 };
    for (int s = 0; s < nsegs; ++s) {
        ctx.seg_base = out->pieces;
        ctx.src = s;
        (void) subdivide_seg(segs[s], 0.0, 1.0, 0, &ctx);
        /* 必须看 ctx.rc：某侧子树出错时整体返回值可能是非零（另一侧已写的段数），
         * 只看返回值会把容量/深度错误吞掉。 */
        if (ctx.rc != BEZ_OK) {
            if (res) {
                res->bad_seg = ctx.bad_seg;
                res->pieces = out->pieces;
            }
            return ctx.rc;
        }
    }
    if (res) res->pieces = out->pieces;
    return BEZ_OK;
}

/*
 * 按给定容差细分。容差 T 对应的弦高误差界为 (3/4)*T；
 * 调用方通常先用 bezier_tol_for_error 反解 T，使 (3/4)*T <= E。
 */
int bezier_flatten_tol(const BPoint (*segs)[4], int nsegs, double tol,
                       BFlat *out, BPathResult *res) {
    result_reset(res);
    if (!segs || !out || nsegs < 1 || nsegs > BEZ_MAX_PATH_SEGS) {
        if (res) res->bad_seg = -1;
        return BEZ_E_ARG;
    }
    if (!isfinite(tol) || tol <= 0.0) {
        if (res) { res->bad_seg = -1; res->tol = tol; }
        return BEZ_E_TOL;
    }
    return flatten_path_impl(segs, nsegs, BEZ_CHORD_BOUND * tol, tol, out, res);
}

/*
 * 组合入口：先按 E 反解最大允许容差，再按该容差细分。
 * 不接受调用方直接给容差，避免容差被放大到超过弦高误差上界。
 */
int bezier_flatten_path(const BPoint (*segs)[4], int nsegs, double err_bound,
                        BFlat *out, BPathResult *res) {
    result_reset(res);
    if (!segs || !out || nsegs < 1 || nsegs > BEZ_MAX_PATH_SEGS) {
        if (res) res->bad_seg = -1;
        return BEZ_E_ARG;
    }
    if (!isfinite(err_bound) || err_bound <= 0.0) {
        if (res) { res->bad_seg = -1; res->err_bound = err_bound; }
        return BEZ_E_TOL;
    }
    double tol = bezier_tol_for_error(err_bound);
    if (!(tol > 0.0)) {
        if (res) res->bad_seg = -1;
        return BEZ_E_TOL;
    }
    int rc = flatten_path_impl(segs, nsegs, err_bound, tol, out, res);
    if (res && rc == BEZ_OK) res->err_bound = err_bound;  /* 保留调用方的 E */
    return rc;
}

/* ------------------------------------------------------------------ */
/* 误差量测与验证                                                      */
/* ------------------------------------------------------------------ */

/* 对参数 u 处的点到折线段的距离。 */
static double err_at(const BPoint c[4], double t0, double t1,
                     BPoint a, BPoint b, double u) {
    BPoint p = bezier_eval(c, t0 + (t1 - t0) * u);
    return dist_point_seg(p, a, b);
}

/* 粗采样找峰值，再在相邻区间做黄金分割细化到 ~1e-9 的 u 分辨率。 */
double bezier_piece_error(const BPoint c[4], double t0, double t1,
                          BPoint a, BPoint b, int samples) {
    if (!c || !(t1 > t0)) return -1.0;
    if (samples < 8) samples = 8;
    if (samples > 65536) samples = 65536;
    double h = 1.0 / (double)samples;
    double best_u = 0.0, best = err_at(c, t0, t1, a, b, 0.0);
    for (int i = 1; i <= samples; ++i) {
        double u = (double)i * h;
        double e = err_at(c, t0, t1, a, b, u);
        if (e > best) { best = e; best_u = u; }
    }
    double lo = best_u - h < 0.0 ? 0.0 : best_u - h;
    double hi = best_u + h > 1.0 ? 1.0 : best_u + h;
    const double gr = 0.6180339887498949;
    double x1 = hi - gr * (hi - lo), x2 = lo + gr * (hi - lo);
    double f1 = err_at(c, t0, t1, a, b, x1), f2 = err_at(c, t0, t1, a, b, x2);
    for (int it = 0; it < 200 && (hi - lo) > 1e-12; ++it) {
        if (f1 < f2) { lo = x1; x1 = x2; f1 = f2; x2 = lo + gr * (hi - lo);
                       f2 = err_at(c, t0, t1, a, b, x2); }
        else         { hi = x2; x2 = x1; f2 = f1; x1 = hi - gr * (hi - lo);
                       f1 = err_at(c, t0, t1, a, b, x1); }
    }
    double fine = f1 > f2 ? f1 : f2;
    return fine > best ? fine : best;
}

int bezier_verify_path(const BPoint (*segs)[4], int nsegs, const BFlat *f,
                       double err_bound, int samples, BPathResult *res) {
    if (!segs || !f || !res || nsegs < 1 || nsegs > BEZ_MAX_PATH_SEGS)
        return BEZ_E_ARG;
    if (!isfinite(err_bound) || err_bound <= 0.0) return BEZ_E_TOL;
    res->max_err = 0.0;
    res->violations = 0;
    res->pass = 1;
    res->worst_seg = -1;
    res->worst_piece = -1;
    res->worst_piece_in_seg = -1;
    int in_seg = 0;
    for (int i = 0; i < f->pieces; ++i) {
        int s = f->seg[i];
        if (s < 0 || s >= nsegs) return BEZ_E_ARG;
        if (i == 0 || f->seg[i - 1] != s) in_seg = 0;
        double e = bezier_piece_error(segs[s], f->t0[i], f->t1[i],
                                      f->pts[i], f->pts[i + 1], samples);
        if (e < 0.0) return BEZ_E_ARG;
        if (e > res->max_err || res->worst_piece < 0) {
            res->max_err = e;
            res->worst_seg = s;
            res->worst_piece = i;
            res->worst_piece_in_seg = in_seg;
        }
        if (!(e <= err_bound)) {
            ++res->violations;
            res->pass = 0;
        }
        ++in_seg;
    }
    return BEZ_OK;
}

/* ------------------------------------------------------------------ */
/* 显式模式开关                                                        */
/* ------------------------------------------------------------------ */

int bezier_flatten_mode(const BPoint c[4], double tol_or_err, BMode mode,
                        BFlat *out, BPathResult *res) {
    result_reset(res);
    if (!c || !out) {
        if (res) res->bad_seg = -1;
        return BEZ_E_ARG;
    }
    if (mode == BEZ_MODE_LEGACY_FIXED) {
        /* 与 bezier_flatten 相同的 8 段：直接复用同一求值循环，位级一致。 */
        double tol = tol_or_err;
        if (tol <= 0.0) {   /* 与旧接口同一条判据：tol <= 0 视为非法 */
            if (res) res->bad_seg = -1;
            return BEZ_E_TOL;
        }
        out->pieces = 8;
        for (int i = 0; i <= 8; ++i) {
            out->pts[i] = bezier_eval(c, (double)i / 8.0);
            if (i < 8) {
                out->t0[i] = (double)i / 8.0;
                out->t1[i] = (double)(i + 1) / 8.0;
                out->seg[i] = 0;
            }
        }
        if (res) {
            res->nsegs = 1;
            res->pieces = 8;
            res->err_bound = tol_or_err;
            res->tol = tol_or_err;
        }
        return BEZ_OK;
    }
    if (mode != BEZ_MODE_ADAPTIVE) {
        if (res) res->bad_seg = -1;
        return BEZ_E_ARG;
    }
    BPoint one[1][4];
    for (int i = 0; i < 4; ++i) one[0][i] = c[i];
    return bezier_flatten_path(one, 1, tol_or_err, out, res);
}
