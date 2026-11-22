#include "warp.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- 基础：贝塞尔求值、速率、累计弧长 ---------- */

static WPoint bez_eval(const WPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u*u*u, b = 3.0*u*u*t, d = 3.0*u*t*t, e = t*t*t;
    WPoint p;
    p.x = a*c[0].x + b*c[1].x + d*c[2].x + e*c[3].x;
    p.y = a*c[0].y + b*c[1].y + d*c[2].y + e*c[3].y;
    return p;
}

static WPoint bez_vel(const WPoint c[4], double t) {
    double u = 1.0 - t;
    WPoint v;
    v.x = 3.0*u*u*(c[1].x - c[0].x)
        + 6.0*u*t*(c[2].x - c[1].x)
        + 3.0*t*t*(c[3].x - c[2].x);
    v.y = 3.0*u*u*(c[1].y - c[0].y)
        + 6.0*u*t*(c[2].y - c[1].y)
        + 3.0*t*t*(c[3].y - c[2].y);
    return v;
}

static double speed(const WPoint c[4], double t) {
    WPoint v = bez_vel(c, t);
    return sqrt(v.x*v.x + v.y*v.y);
}

static double simpson(const WPoint c[4], double a, double b) {
    double m = 0.5*(a + b);
    return (b - a)/6.0 * (speed(c, a) + 4.0*speed(c, m) + speed(c, b));
}

static double asr(const WPoint c[4], double a, double b,
                  double whole, double eps, int depth) {
    double m = 0.5*(a + b);
    double left = simpson(c, a, m);
    double right = simpson(c, m, b);
    if (depth <= 0 || fabs(left + right - whole) <= 15.0*eps)
        return left + right + (left + right - whole)/15.0;
    return asr(c, a, m, left, eps*0.5, depth - 1)
         + asr(c, m, b, right, eps*0.5, depth - 1);
}

double warp_arclen(const WPoint c[4], double t) {
    if (!c || t <= 0.0) return 0.0;
    if (t >= 1.0) t = 1.0;
    double whole = simpson(c, 0.0, 1.0);
    /* 积分容差随目标长度缩放，绝对下限 1e-13。 */
    double eps = fmax(1e-13, 1e-12*fmax(whole, 1.0));
    if (t < 1.0)
        return asr(c, 0.0, t, simpson(c, 0.0, t), eps*t, 24);
    return asr(c, 0.0, 1.0, whole, eps, 24);
}

/* 参考长度量级：端弦，退化时退回控制多边形长度。 */
static double ref_length(const WPoint c[4]) {
    double dx = c[3].x - c[0].x, dy = c[3].y - c[0].y;
    double L = sqrt(dx*dx + dy*dy);
    if (L > 1e-12) return L;
    L = 0.0;
    for (int i = 0; i < 3; ++i) {
        double ex = c[i+1].x - c[i].x, ey = c[i+1].y - c[i].y;
        L += sqrt(ex*ex + ey*ey);
    }
    return L;
}

/* 新路径用：全参数域扫描速率极值，供自适应 omega 与压缩性分析。 */
static void speed_extrema(const WPoint c[4], double *vmin, double *vmax) {
    double lo = 1e300, hi = 0.0;
    for (int i = 0; i <= 4096; ++i) {
        double v = speed(c, (double)i / 4096.0);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
    }
    *vmin = lo;
    *vmax = hi;
}

static int finite_ctrl(const WPoint c[4], char *field) {
    for (int k = 0; k < 4; ++k) {
        if (!isfinite(c[k].x)) { if (field) snprintf(field, WARP_FIELD_MAX, "c[%d].x", k); return 0; }
        if (!isfinite(c[k].y)) { if (field) snprintf(field, WARP_FIELD_MAX, "c[%d].y", k); return 0; }
    }
    return 1;
}

static double clamp01(double t) {
    if (t < 0.0) return 0.0;
    if (t > 1.0) return 1.0;
    return t;
}

/* ---------- 旧路径：固定 8 次，无停止判据 ---------- */

int warp_correct_legacy(const WPoint c[4], double target_s, double t0,
                        double *out_t) {
    if (!c || !out_t) return WARP_E_ARG;
    if (!finite_ctrl(c, NULL)) return WARP_E_CTRL;
    double L = ref_length(c);
    if (!(L > 0.0)) return WARP_E_DEGEN;
    double t = clamp01(t0);
    for (int k = 0; k < WARP_LEGACY_ITERS; ++k) {
        double r = (target_s - warp_arclen(c, t)) / L;
        t = clamp01(t + r);   /* legacy: omega 固定为 1，次数固定为 8 */
    }
    *out_t = t;
    return WARP_OK;
}

/* ---------- 新路径：不动点收敛 ---------- */

static void set_field(WarpReport *rep, const char *name) {
    if (rep) snprintf(rep->field, WARP_FIELD_MAX, "%s", name);
}

int warp_correct(const WPoint c[4], double target_s, double t0,
                 unsigned flags, const WarpCfg *cfg,
                 WPoint *point, WarpReport *rep) {
    if (rep) memset(rep, 0, sizeof *rep);
    if (!c) { set_field(rep, "c"); return WARP_E_ARG; }
    if (!finite_ctrl(c, rep ? rep->field : NULL)) return WARP_E_CTRL;
    if (!isfinite(target_s)) { set_field(rep, "s"); return WARP_E_TARGET; }
    if (!isfinite(t0)) { set_field(rep, "t0"); return WARP_E_ARG; }

    double L = ref_length(c);
    if (!(L > 0.0)) { set_field(rep, "c"); return WARP_E_DEGEN; }
    double total = warp_arclen(c, 1.0);
    if (target_s < 0.0 || target_s > total) { set_field(rep, "s"); return WARP_E_TARGET; }

    /* 关闭新逻辑：现状行为，逐位复刻 legacy 的 8 次固定迭代。 */
    if (!(flags & WARP_F_FIXPOINT)) {
        double t;
        int rc = warp_correct_legacy(c, target_s, t0, &t);
        if (rc != WARP_OK) return rc;
        if (rep) {
            rep->status = WARP_OK;
            rep->iters = WARP_LEGACY_ITERS;
            rep->t = t;
            rep->residual = fabs(target_s - warp_arclen(c, t));
            rep->step = 0.0;
        }
        if (point) *point = bez_eval(c, t);
        return WARP_OK;
    }

    /* 新路径改用总弧长 total 归一化（上方 L 原是端弦，仅供 legacy 与退化检查）。 */
    L = total;
    double vmin, vmax;
    speed_extrema(c, &vmin, &vmax);

    double tol_len = 1e-10, tol_t = 1e-12, eta = 0.8;
    double fixed_om = 0.0;       /* 0 表示自适应 omega */
    int max_iter = WARP_DEFAULT_MAX;
    if (cfg) {
        if (cfg->tol_len > 0.0) tol_len = cfg->tol_len;
        if (cfg->tol_t > 0.0) tol_t = cfg->tol_t;
        if (cfg->max_iter > 0) max_iter = cfg->max_iter;
        if (cfg->omega > 0.0) fixed_om = cfg->omega;
        if (cfg->osc_eta > 0.0) eta = cfg->osc_eta;
    }
    if (!(tol_len > 0.0) || !isfinite(tol_len)) { set_field(rep, "tol_len"); return WARP_E_TOL; }
    if (!(tol_t > 0.0) || !isfinite(tol_t)) { set_field(rep, "tol_t"); return WARP_E_TOL; }
    if (max_iter > 4096) { set_field(rep, "max_iter"); return WARP_E_TOL; }
    /* 固定 omega 的合法区间 (0,2)：2L/v 压缩边界的最宽松上界即 v/L<=4 时 omega<0.5，
     * 自适应路径不受此限；此处只拦截数学上必然发散/越界的值。 */
    if (fixed_om != 0.0 && (!(fixed_om > 0.0 && fixed_om < 2.0) || !isfinite(fixed_om))) {
        set_field(rep, "omega"); return WARP_E_TOL;
    }
    if (!(eta > 0.0 && eta <= 1.0) || !isfinite(eta)) {
        set_field(rep, "osc_eta"); return WARP_E_TOL;
    }
    /* 自适应上限：高速区保证 |1-omega*v/L|<=0.8，即 cap<=1.8L/vmax；
     * 低速区的放大项 0.9L/v 不受此 cap 影响（那里 omega 取到 0.9L/v）。 */
    double om_cap = fmin(0.9, 1.8 * L / vmax);
    (void)vmin;

    double t = clamp01(t0);
    double prev_step = 0.0, prev_delta = 0.0;
    int prev_sign = 0;
    if (rep) rep->status = WARP_NO_CONVERGE;

    for (int k = 1; k <= max_iter; ++k) {
        double S = warp_arclen(c, t);
        double resid = target_s - S;
        double ares = fabs(resid);
        double vt = speed(c, t);
        /* 自适应：低速区放大到 0.9L/v（|g'|=0.1），高速区由 om_cap 压住；
         * 用户指定固定 omega 时按指定值跑，用于振荡专项与复现实验。 */
        double om = fixed_om > 0.0 ? fixed_om : fmin(om_cap, 0.9 * L / vt);
        double delta = om * resid / L;
        double t_new = clamp01(t + delta);
        double step = fabs(t_new - t);
        int sgn = (delta > 0.0) - (delta < 0.0);

        if (rep) {
            rep->iters = k;
            rep->t = t_new;
            rep->residual = ares;
            rep->step = step;
            if (k >= 2 && prev_step > 0.0)
                rep->rate = step / prev_step;
        }

        /* 主判据：弧长残差达标；次判据：参数已不动（含撞边界）。 */
        if (ares <= tol_len || step <= tol_t) {
            if (rep) rep->status = WARP_OK;
            if (point) *point = bez_eval(c, t_new);
            return WARP_OK;
        }

        /* 振荡：方向反转且幅度不衰减（连续两轮即 2-周期）。 */
        if (k >= 2 && sgn != 0 && sgn == -prev_sign &&
            fabs(delta) >= eta * fabs(prev_delta)) {
            if (rep) {
                rep->status = WARP_OSCILLATING;
                rep->osc_period = 2;
                rep->osc_step = k;
            }
            return WARP_OSCILLATING;
        }

        prev_sign = sgn;
        prev_delta = delta;
        prev_step = step;
        t = t_new;
    }

    /* 耗尽上限：残差未达标，拒绝，point 不写。 */
    if (rep) {
        rep->status = WARP_NO_CONVERGE;
        rep->t = t;
        rep->residual = fabs(target_s - warp_arclen(c, t));
    }
    return WARP_NO_CONVERGE;
}

/* ---------- 弧长等分 ---------- */

int warp_equally_spaced(const WPoint c[4], int n, unsigned flags,
                        const WarpCfg *cfg, WPoint *out, WarpReport *reps) {
    if (!c || !out) return WARP_E_ARG;
    if (n < 2 || n > 256) return WARP_E_ARG;
    if (!finite_ctrl(c, NULL)) return WARP_E_CTRL;
    double total = warp_arclen(c, 1.0);
    if (!(total > 0.0)) return WARP_E_DEGEN;

    int worst = WARP_OK;
    for (int i = 0; i < n; ++i) {
        double s = total * (double)i / (double)(n - 1);
        if (i == 0) {
            out[i] = bez_eval(c, 0.0);
            if (reps) {
                memset(&reps[i], 0, sizeof reps[i]);
                reps[i].status = WARP_OK; reps[i].t = 0.0;
            }
            continue;
        }
        if (i == n - 1) {
            out[i] = bez_eval(c, 1.0);
            if (reps) {
                memset(&reps[i], 0, sizeof reps[i]);
                reps[i].status = WARP_OK; reps[i].t = 1.0;
            }
            continue;
        }
        /* 初值取线性参数，等分点附近通常已足够近。 */
        double t0 = (double)i / (double)(n - 1);
        WarpReport local;
        int rc = warp_correct(c, s, t0, flags, cfg, &out[i], &local);
        if (reps) reps[i] = local;
        if (rc != WARP_OK && worst == WARP_OK) worst = rc;
    }
    return worst;
}

/* ---------- 产物导出 ---------- */

int warp_save_pgm(const char *path, const WPoint *pts, int n,
                  double minx, double miny, double maxx, double maxy,
                  int w, int h) {
    if (!path || !pts || n < 1 || w <= 0 || h <= 0) return WARP_E_ARG;
    if (!(maxx > minx) || !(maxy > miny)) return WARP_E_ARG;
    FILE *f = fopen(path, "wb");
    if (!f) return WARP_E_IO;
    unsigned char *buf = (unsigned char *)calloc((size_t)w * (size_t)h, 1);
    if (!buf) { fclose(f); return WARP_E_IO; }
    for (int i = 0; i < n; ++i) {
        int gx = (int)floor((pts[i].x - minx) / (maxx - minx) * w);
        int gy = (int)floor((maxy - pts[i].y) / (maxy - miny) * h);
        if (gx < 0 || gx >= w || gy < 0 || gy >= h) continue;
        buf[gy * w + gx] = 255;
    }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    free(buf);
    fclose(f);
    return WARP_OK;
}

int warp_save_report_csv(const char *path, const WarpReport *r, int n) {
    if (!path || !r || n < 1) return WARP_E_ARG;
    FILE *f = fopen(path, "wb");
    if (!f) return WARP_E_IO;
    fprintf(f, "index,status,iters,t,residual,step,rate,osc_period,osc_step,field\n");
    for (int i = 0; i < n; ++i)
        fprintf(f, "%d,%d,%d,%.15g,%.15g,%.15g,%.15g,%d,%d,%s\n",
                i, r[i].status, r[i].iters, r[i].t, r[i].residual,
                r[i].step, r[i].rate, r[i].osc_period, r[i].osc_step,
                r[i].field);
    int bad = ferror(f);
    fclose(f);
    return bad ? WARP_E_IO : WARP_OK;
}
