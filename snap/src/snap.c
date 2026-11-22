#include "snap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 基础工具 ---------- */

static int finite_pt(SnapPoint p) {
    return isfinite(p.x) && isfinite(p.y);
}

/* 与 C99 round 一致：halfway 远离零。误差落在 [-0.5, 0.5]。 */
static double snap_round(double v) { return round(v); }

/* 弧长表的构建（顺序求和，与 gold/ref points 一致）。
 * 零长线段（相邻点重合）同样拒绝：那是退化输入而不是闭合折线。 */
static int build_table(const SnapPoint *pts, int n, double **out_s, double *out_total) {
    double *s = (double *)malloc((size_t)n * sizeof *s);
    if (!s) return SNAP_E_NOMEM;
    s[0] = 0.0;
    for (int i = 1; i < n; ++i) {
        double dx = pts[i].x - pts[i-1].x, dy = pts[i].y - pts[i-1].y;
        double l = sqrt(dx * dx + dy * dy);
        if (l == 0.0) { free(s); return SNAP_E_DEGENERATE; }
        s[i] = s[i-1] + l;
    }
    *out_s = s;
    *out_total = s[n-1];
    return SNAP_OK;
}

/* 按累计弧长取点（与 gold/ref points 的分段定位规则一致）。 */
static void sample_arc(const SnapPoint *pts, const double *s, int n, double t,
                       SnapPoint *out) {
    int seg = 0;
    for (; seg + 1 < n - 1; ++seg) {
        if (s[seg+1] >= t) break;
    }
    double span = s[seg+1] - s[seg];
    double f = span > 1e-12 ? (t - s[seg]) / span : 0.0;
    out->x = pts[seg].x + (pts[seg+1].x - pts[seg].x) * f;
    out->y = pts[seg].y + (pts[seg+1].y - pts[seg].y) * f;
}

/* ---------- 目标函数 ---------- */

typedef struct {
    double max_err;  /* max 欧氏取整误差 */
    double axis_err; /* max(|ex|,|ey|) */
    double rms_err;  /* 均方根 */
} SnapMetrics;

/* 评价 d：m 个等分点，L=(m-1)d。cutoff>0 时做早停：
 * 一旦某个点的误差超过 cutoff 就直接放弃（该候选不可能更好，理由见
 * snap.h 的字典序：max 单调决定半像素违反量与后续比较项）。
 * 返回 1 表示被剪枝，0 表示完整求出。
 * 分段定位用游标前进：t_k = k*d 单调，整轮摊还 O(n + m)。 */
static int eval_d(const SnapPoint *pts, const double *s, int n, int m,
                  double d, double cutoff, SnapMetrics *out) {
    double maxe = 0.0, axis = 0.0, sumsq = 0.0;
    int seg = 0;
    for (int k = 0; k < m; ++k) {
        double t = d * (double)k;
        while (seg + 1 < n - 1 && s[seg+1] < t) ++seg;
        double span = s[seg+1] - s[seg];
        double f = span > 1e-12 ? (t - s[seg]) / span : 0.0;
        double x = pts[seg].x + (pts[seg+1].x - pts[seg].x) * f;
        double y = pts[seg].y + (pts[seg+1].y - pts[seg].y) * f;
        double ex = x - snap_round(x);
        double ey = y - snap_round(y);
        double ae = fabs(ex), ay = fabs(ey);
        if (ae > axis) axis = ae;
        if (ay > axis) axis = ay;
        double e = sqrt(ex * ex + ey * ey);
        if (e > maxe) maxe = e;
        sumsq += e * e;
        if (cutoff > 0.0 && maxe > cutoff) return 1;
    }
    out->max_err = maxe;
    out->axis_err = axis;
    out->rms_err = sqrt(sumsq / (double)m);
    return 0;
}

/* 字典序比较：a 优于 b 返回 1。 */
static int score_better(double a_max, double a_rms, double a_d,
                        double b_max, double b_rms, double b_d) {
    double av = a_max > SNAP_HALF_PIXEL ? a_max - SNAP_HALF_PIXEL : 0.0;
    double bv = b_max > SNAP_HALF_PIXEL ? b_max - SNAP_HALF_PIXEL : 0.0;
    if (av < bv - 1e-12) return 1;
    if (av > bv + 1e-12) return 0;

    int a_ex = a_max <= SNAP_EXACT_EPS;
    int b_ex = b_max <= SNAP_EXACT_EPS;
    if (a_ex != b_ex) return a_ex;

    if (!a_ex) {
        if (a_max < b_max - 1e-12) return 1;
        if (a_max > b_max + 1e-12) return 0;
        if (a_rms < b_rms - 1e-12) return 1;
        if (a_rms > b_rms + 1e-12) return 0;
    }
    /* 误差打平：取更长的总长 */
    return a_d > b_d + 1e-12;
}

/* ---------- 候选生成 ---------- */

typedef struct {
    double *v;
    size_t n, cap;
    int oom;
} CandVec;

static void cand_push(CandVec *c, double d) {
    if (c->oom) return;
    if (c->n == c->cap) {
        size_t nc = c->cap ? c->cap * 2 : 4096;
        double *nv = (double *)realloc(c->v, nc * sizeof *nv);
        if (!nv) { c->oom = 1; return; }
        c->v = nv;
        c->cap = nc;
    }
    c->v[c->n++] = d;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    if (x < y) return -1;
    if (x > y) return 1;
    return 0;
}

/* 对每个线段，把与整数 x=const / y=const 直线的交点按弧长记下来。
 * 整数格点（x、y 同时为整数）一定同时出现在两组里；这里只要候选，
 * 后续会按 k 反解 d = s/k。返回穿越次数；超出预算时返回 -1（只用网格）。 */
#define SNAP_CROSS_BUDGET 2000000L
#define LIMIT_COORD 1e12

static long collect_crossings(const SnapPoint *pts, const double *s, int n,
                              double *out, long maxout) {
    long cnt = 0;
    /* 坐标量级过大时整数遍历既无意义也可能溢出 long，直接放弃候选生成。 */
    for (int i = 0; i + 1 < n; ++i) {
        SnapPoint a = pts[i], b = pts[i+1];
        double l = s[i+1] - s[i];
        if (!(l > 1e-12)) continue;
        double dx = b.x - a.x, dy = b.y - a.y;
        if (fabs(a.x) > LIMIT_COORD || fabs(b.x) > LIMIT_COORD ||
            fabs(a.y) > LIMIT_COORD || fabs(b.y) > LIMIT_COORD) return -1;

        if (dx != 0.0) {
            double lo = a.x < b.x ? a.x : b.x;
            double hi = a.x < b.x ? b.x : a.x;
            double ilo = ceil(lo - 1e-9), ihi = floor(hi + 1e-9);
            if (ihi - ilo + 1.0 > 1e7) return -1;
            for (long xi = (long)ilo; (double)xi <= ihi; ++xi) {
                double t = ((double)xi - a.x) / dx;
                if (t < -1e-9 || t > 1.0 + 1e-9) continue;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                if (cnt >= maxout) return -1;
                out[cnt++] = s[i] + t * l;
            }
        }
        if (dy != 0.0) {
            double lo = a.y < b.y ? a.y : b.y;
            double hi = a.y < b.y ? b.y : a.y;
            double ilo = ceil(lo - 1e-9), ihi = floor(hi + 1e-9);
            if (ihi - ilo + 1.0 > 1e7) return -1;
            for (long yi = (long)ilo; (double)yi <= ihi; ++yi) {
                double t = ((double)yi - a.y) / dy;
                if (t < -1e-9 || t > 1.0 + 1e-9) continue;
                if (t < 0.0) t = 0.0;
                if (t > 1.0) t = 1.0;
                if (cnt >= maxout) return -1;
                out[cnt++] = s[i] + t * l;
            }
        }
    }
    return cnt;
}

/* 搜索精调用的起点列表 */
#define SNAP_SEEDS 8
typedef struct {
    double d[SNAP_SEEDS];
    double max[SNAP_SEEDS], rms[SNAP_SEEDS];
    int n;
} TopSeeds;

static void seed_offer(TopSeeds *t, double d, double mx, double rms, int keep) {
    int pos = -1;
    for (int i = 0; i < t->n; ++i)
        if (fabs(t->d[i] - d) < 1e-12) pos = i;
    if (pos >= 0) {
        if (score_better(mx, rms, d, t->max[pos], t->rms[pos], t->d[pos])) {
            t->max[pos] = mx; t->rms[pos] = rms;
        }
        return;
    }
    if (t->n < keep) {
        pos = t->n++;
    } else {
        int worst = 0;
        for (int i = 1; i < t->n; ++i)
            if (score_better(t->max[worst], t->rms[worst], t->d[worst],
                             t->max[i], t->rms[i], t->d[i])) worst = i;
        if (!score_better(mx, rms, d, t->max[worst], t->rms[worst], t->d[worst])) return;
        pos = worst;
    }
    t->d[pos] = d; t->max[pos] = mx; t->rms[pos] = rms;
}

/* ---------- 主求解 ---------- */

int snap_solve(const SnapPoint *pts, int n, int m, SnapResult *out,
               SnapNode *nodes, int maxnodes) {
    if (!pts || !out || n < 2 || m < 2) return SNAP_E_ARG;
    if (nodes && maxnodes < m) return SNAP_E_CAP;
    memset(out, 0, sizeof *out);
    out->m = m;
    out->detail[0] = '\0';

    for (int i = 0; i < n; ++i)
        if (!finite_pt(pts[i])) {
            snprintf(out->detail, sizeof out->detail,
                     "坐标不是有限数（第 %d 个点）", i);
            return SNAP_E_DEGENERATE;
        }

    if (fabs(pts[0].x - pts[n-1].x) <= 1e-9 &&
        fabs(pts[0].y - pts[n-1].y) <= 1e-9) {
        snprintf(out->detail, sizeof out->detail,
                 "REJECT closed: 起点 (%.6f, %.6f) 与终点重合，闭合折线不做等分",
                 pts[0].x, pts[0].y);
        return SNAP_E_CLOSED;
    }

    double *s = NULL, total = 0.0;
    int rc = build_table(pts, n, &s, &total);
    if (rc != SNAP_OK) {
        snprintf(out->detail, sizeof out->detail,
                 rc == SNAP_E_DEGENERATE ? "REJECT degenerate: 零长线段或总长为零"
                                         : "REJECT nomem: 内存不足");
        return rc;
    }
    if (!(total > 0.0)) {
        free(s);
        snprintf(out->detail, sizeof out->detail, "REJECT degenerate: 总长为零");
        return SNAP_E_DEGENERATE;
    }

    double dlo = total / (double)m;             /* L = (m-1)d >= (m-1)/m * T */
    double dhi = total / (double)(m - 1);       /* L <= T */
    double d_in = 1e-9 * (dhi > 0 ? dhi : 1.0);

    TopSeeds seeds;
    memset(&seeds, 0, sizeof seeds);
    double best_d = dhi, best_max = 1e30, best_rms = 1e30;
    int have_best = 0;
    unsigned long long evals = 0;

    /* 一个候选的完整流程：评价、更新全局最优、喂给精调起点。 */
    #define SNAP_CONSIDER(D) do {                                              \
        double _d = (D);                                                       \
        if (_d < dlo - d_in) _d = dlo;                                         \
        if (_d > dhi + d_in) _d = dhi;                                         \
        if (_d >= dlo - d_in && _d <= dhi + d_in) {                            \
            SnapMetrics _mt;                                                   \
            double _cut = have_best ? best_max : 0.0;                          \
            if (eval_d(pts, s, n, m, _d, _cut, &_mt) == 0) {                   \
                ++evals;                                                       \
                if (!have_best || score_better(_mt.max_err, _mt.rms_err, _d,   \
                                               best_max, best_rms, best_d)) {  \
                    have_best = 1; best_d = _d;                                \
                    best_max = _mt.max_err; best_rms = _mt.rms_err;            \
                }                                                              \
                seed_offer(&seeds, _d, _mt.max_err, _mt.rms_err, SNAP_SEEDS);           \
            } else {                                                           \
                ++evals;                                                       \
            }                                                                  \
        }                                                                      \
    } while (0)

    /* 候选 1：折线顶点弧长 s，k*d = s -> d = s/k。
     * 候选 2：整数格点穿越弧长 s（k=1 的点必须双坐标整数的必要条件）。 */
    CandVec cv;
    memset(&cv, 0, sizeof cv);

    for (int i = 0; i < n; ++i)
        for (int k = 1; k < m; ++k) {
            double d = s[i] / (double)k;
            if (d >= dlo - d_in && d <= dhi + d_in) cand_push(&cv, d);
        }

    /* 候选 2 只在量级合理时生成；超预算或量级过大时退化为网格 + 精调。 */
    if (!(total > LIMIT_COORD)) {
        long max_cross = SNAP_CROSS_BUDGET;
        double *cross = (double *)malloc((size_t)max_cross * sizeof *cross);
        if (cross) {
            long nc = collect_crossings(pts, s, n, cross, max_cross);
            if (nc > 0) {
                /* 控制评价量：穿越数 x (m-1) 超预算时按步长抽样。 */
                long want = nc * (long)(m - 1);
                long stride = 1;
                if (want > 4000000L) stride = (want + 3999999L) / 4000000L;
                for (long i = 0; i < nc; i += stride)
                    for (int k = 1; k < m; ++k) {
                        double d = cross[i] / (double)k;
                        if (d >= dlo - d_in && d <= dhi + d_in) cand_push(&cv, d);
                    }
            }
            free(cross);
        }
    }

    /* 候选 3：全区间粗网格 */
    const int GRID = 2048;
    for (int i = 0; i < GRID; ++i)
        cand_push(&cv, dlo + (dhi - dlo) * (double)i / (double)(GRID - 1));

    if (cv.oom) {
        free(cv.v);
        free(s);
        snprintf(out->detail, sizeof out->detail, "内存不足");
        return SNAP_E_NOMEM;
    }

    if (cv.n > 1) {
        qsort(cv.v, cv.n, sizeof *cv.v, cmp_double);
        size_t w = 1;
        for (size_t i = 1; i < cv.n; ++i) {
            double rel = fabs(cv.v[i]) > 1.0 ? fabs(cv.v[i]) : 1.0;
            if (fabs(cv.v[i] - cv.v[w-1]) > 1e-12 * rel) cv.v[w++] = cv.v[i];
        }
        cv.n = w;
    }
    for (size_t i = 0; i < cv.n; ++i) SNAP_CONSIDER(cv.v[i]);
    free(cv.v);

    /* 精调：从若干最优起点做多轮收缩（每轮 25 个采样，步长 /8）。 */
    int nseed = seeds.n;
    double seed_d[SNAP_SEEDS + 1];
    int ns = 0;
    for (int i = 0; i < nseed; ++i) seed_d[ns++] = seeds.d[i];
    if (have_best) seed_d[ns++] = best_d;
    double base_span = (dhi - dlo) / (double)(GRID - 1) * 8.0;
    if (base_span <= 0.0) base_span = dhi * 1e-6;
    for (int si = 0; si < ns; ++si) {
        double d = seed_d[si];
        double span = base_span;
        for (int it = 0; it < 7; ++it) {
            double lo = d - span * 0.5, hi = d + span * 0.5;
            if (lo < dlo) lo = dlo;
            if (hi > dhi) hi = dhi;
            if (!(hi > lo)) { span /= 8.0; continue; }
            double local_d = d, local_max = 1e30, local_rms = 1e30;
            int local_have = 0;
            const int NP = 25;
            for (int j = 0; j < NP; ++j) {
                double c = lo + (hi - lo) * (double)j / (double)(NP - 1);
                SnapMetrics mt;
                double cut = have_best ? best_max : 0.0;
                ++evals;
                if (eval_d(pts, s, n, m, c, cut, &mt) != 0) continue;
                if (!local_have ||
                    score_better(mt.max_err, mt.rms_err, c,
                                 local_max, local_rms, local_d)) {
                    local_have = 1; local_d = c;
                    local_max = mt.max_err; local_rms = mt.rms_err;
                }
                if (!have_best ||
                    score_better(mt.max_err, mt.rms_err, c,
                                 best_max, best_rms, best_d)) {
                    have_best = 1; best_d = c;
                    best_max = mt.max_err; best_rms = mt.rms_err;
                }
            }
            if (local_have) d = local_d;
            span /= 8.0;
        }
    }
    #undef SNAP_CONSIDER

    if (!have_best) { /* 理论上不可达：网格至少有一个候选 */
        free(s);
        snprintf(out->detail, sizeof out->detail, "搜索没有找到可行候选");
        return SNAP_E_DEGENERATE;
    }

    /* 用最优 d 完整重算一遍并落盘。 */
    SnapMetrics fin;
    if (eval_d(pts, s, n, m, best_d, 0.0, &fin) != 0) {
        free(s);
        snprintf(out->detail, sizeof out->detail, "内部错误：最终评价失败");
        return SNAP_E_DEGENERATE;
    }

    out->total = total;
    out->step = best_d;
    out->target = best_d * (double)(m - 1);
    out->cover = out->target / total;
    out->max_err = fin.max_err;
    out->axis_err = fin.axis_err;
    out->rms_err = fin.rms_err;
    out->evals = evals;
    out->exact = fin.max_err <= SNAP_EXACT_EPS;
    out->feasible = fin.max_err <= SNAP_HALF_PIXEL;
    out->status = out->exact ? SNAP_STATUS_EXACT
                : (out->feasible ? SNAP_STATUS_NEAREST : SNAP_STATUS_OVER_BUDGET);

    if (out->status == SNAP_STATUS_EXACT)
        snprintf(out->detail, sizeof out->detail,
                 "全部 %d 个点落在整点，目标总长 %.6f（折线总长 %.6f）",
                 m, out->target, total);
    else if (out->status == SNAP_STATUS_NEAREST)
        snprintf(out->detail, sizeof out->detail,
                 "无全整点解；最接近方案最大取整误差 %.6f <= 0.5，目标总长 %.6f",
                 out->max_err, out->target);
    else
        snprintf(out->detail, sizeof out->detail,
                 "半像素内无解；最接近方案最大取整误差 %.6f > 0.5，目标总长 %.6f",
                 out->max_err, out->target);

    if (nodes) {
        for (int k = 0; k < m; ++k) {
            SnapPoint q;
            sample_arc(pts, s, n, best_d * (double)k, &q);
            double rx = snap_round(q.x), ry = snap_round(q.y);
            nodes[k].x = q.x;
            nodes[k].y = q.y;
            nodes[k].rx = rx;
            nodes[k].ry = ry;
            nodes[k].ex = q.x - rx;
            nodes[k].ey = q.y - ry;
            nodes[k].err = sqrt(nodes[k].ex * nodes[k].ex +
                                nodes[k].ey * nodes[k].ey);
        }
    }

    free(s);
    return SNAP_OK;
}

/* ---------- 旧的小数等分（与 gold/ref points 同式） ---------- */

int snap_points_legacy(const SnapPoint *pts, int n, int count, SnapPoint *out) {
    if (!pts || !out || n < 2 || count < 2) return SNAP_E_ARG;
    double total = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        total += sqrt(dx*dx + dy*dy);
    }
    if (!(total > 0.0)) return SNAP_E_DEGENERATE;
    double step = total / (double)(count - 1);
    for (int s = 0; s < count; ++s) {
        double target = step * (double)s, acc = 0.0;
        int seg = 0;
        for (; seg + 1 < n - 1; ++seg) {
            double dx = pts[seg+1].x - pts[seg].x, dy = pts[seg+1].y - pts[seg].y;
            double len = sqrt(dx*dx + dy*dy);
            if (acc + len >= target) break;
            acc += len;
        }
        double dx = pts[seg+1].x - pts[seg].x, dy = pts[seg+1].y - pts[seg].y;
        double len = sqrt(dx*dx + dy*dy);
        double f = len > 1e-12 ? (target - acc) / len : 0.0;
        out[s].x = pts[seg].x + dx * f;
        out[s].y = pts[seg].y + dy * f;
    }
    return SNAP_OK;
}

const char *snap_status_str(int status) {
    switch (status) {
        case SNAP_STATUS_EXACT: return "EXACT";
        case SNAP_STATUS_NEAREST: return "NEAREST";
        case SNAP_STATUS_OVER_BUDGET: return "OVER_BUDGET";
        default: return "UNKNOWN";
    }
}

const char *snap_strerror(int code) {
    switch (code) {
        case SNAP_OK: return "成功";
        case SNAP_E_ARG: return "参数非法";
        case SNAP_E_CLOSED: return "起点与终点重合";
        case SNAP_E_DEGENERATE: return "退化输入";
        case SNAP_E_CAP: return "输出容量不足";
        case SNAP_E_NOMEM: return "内存不足";
        default: return "未知错误";
    }
}
