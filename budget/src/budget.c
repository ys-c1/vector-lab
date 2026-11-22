#include "budget.h"
#include <math.h>
#include <time.h>

const char *budget_strerror(int code) {
    switch (code) {
    case BUDGET_OK: return "ok";
    case BUDGET_ERR_ARG: return "参数非法：模块指针为空或数量越界";
    case BUDGET_ERR_BUDGET: return "预算为零或为负，拒绝分配";
    case BUDGET_ERR_NSEG: return "收益曲线非法：段数越界";
    case BUDGET_ERR_WIDTH: return "收益曲线非法：段宽必须为正且有限";
    case BUDGET_ERR_SLOPE: return "收益曲线非法：边际收益不能为负且必须有限";
    case BUDGET_ERR_CONVEX: return "收益曲线非法：边际收益必须非增（曲线须为凹）";
    }
    return "未知错误";
}

int budget_curve_check(const BCurve *c) {
    if (!c) return BUDGET_ERR_ARG;
    if (c->nseg < 1 || c->nseg > BUDGET_MAX_SEG) return BUDGET_ERR_NSEG;
    for (int k = 0; k < c->nseg; ++k) {
        if (!isfinite(c->width[k]) || c->width[k] <= 0.0) return BUDGET_ERR_WIDTH;
        if (!isfinite(c->slope[k]) || c->slope[k] < 0.0) return BUDGET_ERR_SLOPE;
        if (k && c->slope[k] > c->slope[k - 1]) return BUDGET_ERR_CONVEX;
    }
    return BUDGET_OK;
}

int budget_curve_is_zero(const BCurve *c) {
    if (!c) return 0;
    for (int k = 0; k < c->nseg; ++k)
        if (c->slope[k] != 0.0) return 0;
    return 1;
}

double budget_curve_cap(const BCurve *c) {
    double s = 0.0;
    for (int k = 0; k < c->nseg; ++k) s += c->width[k];
    return s;
}

double budget_curve_gain(const BCurve *c, double x) {
    double g = 0.0, acc = 0.0;
    for (int k = 0; k < c->nseg && x > acc; ++k) {
        double w = c->width[k];
        if (acc + w > x) w = x - acc;
        g += w * c->slope[k];
        acc += c->width[k];
    }
    return g;
}

typedef struct { double slope, width; int mod; } BSeg;

static int valid_args(const BCurve *cs, int n, double budget, BAlloc *out) {
    if (!cs || !out || n < 1 || n > BUDGET_MAX_MOD) return BUDGET_ERR_ARG;
    if (!(budget > 0.0) || !isfinite(budget)) return BUDGET_ERR_BUDGET;
    for (int i = 0; i < n; ++i) {
        int rc = budget_curve_check(&cs[i]);
        if (rc != BUDGET_OK) return rc;
    }
    return BUDGET_OK;
}

int budget_lagrange(const BCurve *cs, int n, double budget,
                    BAlloc *out, int *zero_idx, int *nzero, double *lambda) {
    int rc = valid_args(cs, n, budget, out);
    if (rc != BUDGET_OK) return rc;
    int nz = 0;
    double cap = 0.0;
    for (int i = 0; i < n; ++i) {
        out[i].x = 0.0;
        out[i].gain = 0.0;
        if (budget_curve_is_zero(&cs[i])) {
            if (zero_idx && nz < BUDGET_MAX_MOD) zero_idx[nz] = i;
            ++nz;
        } else {
            cap += budget_curve_cap(&cs[i]);
        }
    }
    if (nzero) *nzero = nz;
    if (lambda) *lambda = 0.0;
    if (cap <= 0.0) return BUDGET_OK; /* 全是零收益模块，无可分 */
    double b = budget < cap ? budget : cap;

    /* 注水法：把所有段按边际收益降序，依次灌满，直到预算耗尽。
     * 边界 lambda 同斜率的一组段按宽度比例分剩余预算。 */
    BSeg segs[BUDGET_MAX_MOD * BUDGET_MAX_SEG];
    int ns = 0;
    for (int i = 0; i < n; ++i) {
        if (budget_curve_is_zero(&cs[i])) continue;
        for (int k = 0; k < cs[i].nseg; ++k) {
            segs[ns].slope = cs[i].slope[k];
            segs[ns].width = cs[i].width[k];
            segs[ns].mod = i;
            ++ns;
        }
    }
    for (int i = 1; i < ns; ++i) { /* 稳定插入排序，斜率降序 */
        BSeg v = segs[i];
        int j = i - 1;
        while (j >= 0 && segs[j].slope < v.slope) { segs[j + 1] = segs[j]; --j; }
        segs[j + 1] = v;
    }
    double cum = 0.0, lam = 0.0;
    int s = 0;
    while (s < ns && cum < b) {
        double sl = segs[s].slope, gw = 0.0;
        int e = s;
        while (e < ns && segs[e].slope == sl) { gw += segs[e].width; ++e; }
        if (cum + gw <= b) {
            for (int t = s; t < e; ++t) out[segs[t].mod].x += segs[t].width;
            cum += gw;
            lam = sl;
            s = e;
        } else {
            double rem = b - cum;
            for (int t = s; t < e; ++t)
                out[segs[t].mod].x += rem * segs[t].width / gw;
            cum = b;
            lam = sl;
        }
    }
    for (int i = 0; i < n; ++i) out[i].gain = budget_curve_gain(&cs[i], out[i].x);
    if (lambda) *lambda = lam;
    return BUDGET_OK;
}

int budget_equal(const BCurve *cs, int n, double budget, BAlloc *out) {
    int rc = valid_args(cs, n, budget, out);
    if (rc != BUDGET_OK) return rc;
    double share = budget / (double)n;
    for (int i = 0; i < n; ++i) {
        out[i].x = share;
        out[i].gain = budget_curve_gain(&cs[i], share);
    }
    return BUDGET_OK;
}

int budget_std_curves(BCurve *out, int max) {
    if (!out || max < 6) return BUDGET_ERR_ARG;
    out[0].name = "tess";      out[0].nseg = 2;
    out[0].width[0] = 20.0;    out[0].slope[0] = 8.0;
    out[0].width[1] = 40.0;    out[0].slope[1] = 2.0;
    out[1].name = "aa";        out[1].nseg = 2;
    out[1].width[0] = 10.0;    out[1].slope[0] = 16.0;
    out[1].width[1] = 30.0;    out[1].slope[1] = 4.0;
    out[2].name = "gamma";     out[2].nseg = 2;
    out[2].width[0] = 10.0;    out[2].slope[0] = 6.0;
    out[2].width[1] = 90.0;    out[2].slope[1] = 1.0;
    out[3].name = "dither";    out[3].nseg = 1;
    out[3].width[0] = 60.0;    out[3].slope[0] = 0.25;
    out[4].name = "watermark"; out[4].nseg = 1;
    out[4].width[0] = 100.0;   out[4].slope[0] = 0.0;
    out[5].name = "grid";      out[5].nseg = 1;
    out[5].width[0] = 100.0;   out[5].slope[0] = 0.0;
    return 6;
}

int budget_std_tiers(double *out, int max) {
    if (!out || max < 4) return BUDGET_ERR_ARG;
    out[0] = 40.0;
    out[1] = 80.0;
    out[2] = 120.0;
    out[3] = 200.0;
    return 4;
}

static int in_set(const int *set, int nset, int v) {
    for (int i = 0; i < nset; ++i)
        if (set[i] == v) return 1;
    return 0;
}

int budget_report(FILE *fp, const BCurve *cs, int n,
                  const double *tiers, int ntier, int use_lagrange) {
    if (!fp || !cs || !tiers || n < 1 || n > BUDGET_MAX_MOD || ntier < 1)
        return BUDGET_ERR_ARG;
    BAlloc al[BUDGET_MAX_MOD], eq[BUDGET_MAX_MOD];
    int zidx[BUDGET_MAX_MOD];
    for (int t = 0; t < ntier; ++t) {
        if (!use_lagrange) { /* 旧版均分报告：格式固定，逐字节不变 */
            int rc = budget_equal(cs, n, tiers[t], al);
            if (rc != BUDGET_OK) return rc;
            double tot = 0.0;
            fprintf(fp, "tier %d budget %.2f\n", t + 1, tiers[t]);
            for (int i = 0; i < n; ++i) {
                fprintf(fp, "  %-10s x=%7.2f gain=%7.2f\n",
                        cs[i].name, al[i].x, al[i].gain);
                tot += al[i].gain;
            }
            fprintf(fp, "  total gain %.2f\n", tot);
            continue;
        }
        int nz = 0;
        double lam = 0.0;
        clock_t t0 = clock();
        int rc = budget_lagrange(cs, n, tiers[t], al, zidx, &nz, &lam);
        clock_t t1 = clock();
        if (rc != BUDGET_OK) return rc;
        rc = budget_equal(cs, n, tiers[t], eq);
        if (rc != BUDGET_OK) return rc;
        double ltot = 0.0, etot = 0.0;
        fprintf(fp, "tier %d budget %.2f lambda %.4f\n", t + 1, tiers[t], lam);
        for (int i = 0; i < n; ++i) {
            if (in_set(zidx, nz, i)) continue; /* 零收益模块单独列出 */
            fprintf(fp, "  %-10s x=%7.2f gain=%7.2f\n",
                    cs[i].name, al[i].x, al[i].gain);
            ltot += al[i].gain;
        }
        for (int i = 0; i < n; ++i) etot += eq[i].gain;
        fprintf(fp, "zero-gain modules:");
        for (int z = 0; z < nz; ++z) fprintf(fp, " %s", cs[zidx[z]].name);
        fprintf(fp, "\n");
        fprintf(fp, "total gain %.2f vs equal %.2f ratio %.3f (>=1.20 %s)\n",
                ltot, etot, ltot / etot,
                ltot >= 1.2 * etot ? "ok" : "SHORT");
        fprintf(fp, "solve %ld us\n",
                (long)((t1 - t0) * 1000000.0 / CLOCKS_PER_SEC));
    }
    return BUDGET_OK;
}
