#define _POSIX_C_SOURCE 199309L
#include "adaptive.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---- splitmix64：固定种子可复现的均匀随机源 ---- */
static unsigned long sm64(unsigned long *state) {
    unsigned long z = (*state += 0x9e3779b97f4a7c15UL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9UL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebUL;
    return z ^ (z >> 31);
}

static double rand_unit(unsigned long *state) {
    /* 取高 53 位映射到 [0,1) */
    return (double)(sm64(state) >> 11) * (1.0 / 9007199254740992.0);
}

/* Acklam 标准正态逆 CDF 有理逼近，|误差| < ~1.15e-9 */
double norm_inv(double p) {
    static const double a[6] = {
        -3.969683028665376e+01,  2.209460984245205e+02,
        -2.759285104469687e+02,  1.383577518672690e+02,
        -3.066479806614716e+01,  2.506628277459239e+00
    };
    static const double b[5] = {
        -5.447609879822406e+01,  1.615858368580409e+02,
        -1.556989798598866e+02,  6.680131188771972e+01,
        -1.328068155288572e+01
    };
    static const double c[6] = {
        -7.784894002430293e-03, -3.223964580411365e-01,
        -2.400758277161838e+00, -2.549732539343734e+00,
         4.374664141464968e+00,  2.938163982698783e+00
    };
    static const double d[4] = {
         7.784695709041462e-03,  3.224671290700398e-01,
         2.445134137142996e+00,  3.754408661907416e+00
    };
    const double plow = 0.02425, phigh = 1.0 - plow;
    double q, r;
    if (p < plow) {
        q = sqrt(-2.0 * log(p));
        return (((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
               ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }
    if (p > phigh) {
        q = sqrt(-2.0 * log(1.0 - p));
        return -(((((c[0]*q + c[1])*q + c[2])*q + c[3])*q + c[4])*q + c[5]) /
                ((((d[0]*q + d[1])*q + d[2])*q + d[3])*q + 1.0);
    }
    q = p - 0.5;
    r = q * q;
    return (((((a[0]*r + a[1])*r + a[2])*r + a[3])*r + a[4])*r + a[5]) * q /
           (((((b[0]*r + b[1])*r + b[2])*r + b[3])*r + b[4])*r + 1.0);
}

static int inside(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static int hit_scene(const RScene *s, double x, double y) {
    for (int k = 0; k < s->npolys; ++k)
        if (inside(&s->polys[k], x, y)) return 1;
    return 0;
}

void radapt_cfg_default(RAdaptCfg *cfg) {
    if (!cfg) return;
    cfg->pilot_n = 16;
    cfg->nmax    = 4096;
    cfg->conf    = 0.95;
    cfg->width   = 0.10;
    cfg->seed    = 0x123456789abcdef0UL;
}

int raster_adaptive(const RScene *s, const RBox *box, int w, int h,
                    const RAdaptCfg *cfg0, unsigned char *buf, RAdaptReport *rep) {
    RAdaptReport local;
    if (!rep) rep = &local;
    memset(rep, 0, sizeof(*rep));
    rep->status = RADAPT_E_ARG;

    RAdaptCfg dflt;
    radapt_cfg_default(&dflt);
    const RAdaptCfg *cfg = cfg0 ? cfg0 : &dflt;

    if (!s || !box || !buf || w <= 0 || h <= 0) return RADAPT_E_ARG;
    if (cfg->pilot_n < 2 || cfg->nmax < cfg->pilot_n) return RADAPT_E_ARG;
    if (!(cfg->conf > 0.0 && cfg->conf < 1.0)) return RADAPT_E_ARG;
    if (!(cfg->width > 0.0 && cfg->width <= 1.0)) return RADAPT_E_ARG;

    const size_t npix = (size_t)w * (size_t)h;
    const int n0 = cfg->pilot_n;
    const double dx = (box->maxx - box->minx) / w;
    const double dy = (box->maxy - box->miny) / h;

    /* 试点统计量：命中和（定均值）与平方和（定无偏方差） */
    int *hits = (int*)malloc(npix * sizeof(int));
    if (!hits) return RADAPT_E_ARG;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            unsigned long st = cfg->seed ^
                (((unsigned long)y * 0x9e3779b97f4a7c15UL) +
                 ((unsigned long)x * 0xc2b2ae3d27d4eb4fUL) + 1UL);
            int hc = 0;
            for (int t = 0; t < n0; ++t) {
                double jx = rand_unit(&st), jy = rand_unit(&st);
                double px = box->minx + (x + jx) * dx;
                double py = box->miny + (y + jy) * dy;
                hc += hit_scene(s, px, py);
            }
            hits[(size_t)y * w + x] = hc;
        }

    /* 单点方差估计：逐像素无偏 s^2，取全图最大值（最宽像素主导） */
    double var_max = 0.0;
    int worst_idx = 0, edge = 0;
    for (size_t i = 0; i < npix; ++i) {
        if (hits[i] != 0 && hits[i] != n0) ++edge;
        double s2 = n0 > 1 ? (double)hits[i] * (n0 - hits[i]) /
                             ((double)n0 * (n0 - 1)) : 0.0;
        if (s2 > var_max) { var_max = s2; worst_idx = (int)i; }
    }

    const double z = norm_inv(0.5 + cfg->conf / 2.0);
    int n;
    int degenerate = (var_max <= 0.0);
    int rejected = 0;
    if (degenerate) {
        /* 退化：所有像素试点结果完全一致，覆盖率只能是 0 或 1，
         * 置信区间宽度为 0，无需追加样本。 */
        n = n0;
    } else {
        double need = 4.0 * z * z * var_max / (cfg->width * cfg->width);
        if (need > (double)cfg->nmax) { rejected = 1; n = cfg->nmax; }
        else n = (int)ceil(need);
        if (n < n0) n = n0;
    }
    rep->pilot_n    = n0;
    rep->samples    = n;
    rep->degenerate = degenerate;
    rep->rejected   = rejected;
    rep->var_hat    = var_max;
    rep->edge_pixels = edge;
    rep->worst_p    = (double)hits[worst_idx] / n0;

    if (rejected) {
        /* 按上限也无法达标：拒，不产出图像 */
        rep->width_pred = 2.0 * z * sqrt(var_max / (double)cfg->nmax);
        rep->width_obs  = rep->width_pred;
        rep->status     = RADAPT_E_REJECT;
        clock_gettime(CLOCK_MONOTONIC, &t1);
        rep->ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                  (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
        free(hits);
        return RADAPT_E_REJECT;
    }

    /* 最终采样：n0 个试点样本复用，再补 n-n0 个；逐像素独立流 */
    double width_obs = 0.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            size_t idx = (size_t)y * w + x;
            unsigned long st = cfg->seed ^
                (((unsigned long)y * 0x9e3779b97f4a7c15UL) +
                 ((unsigned long)x * 0xc2b2ae3d27d4eb4fUL) + 1UL);
            /* 重放试点随机流，保持与第一遍同样的点 */
            for (int t = 0; t < n0; ++t) { (void)rand_unit(&st); (void)rand_unit(&st); }
            int hc = hits[idx];
            for (int t = n0; t < n; ++t) {
                double jx = rand_unit(&st), jy = rand_unit(&st);
                double px = box->minx + (x + jx) * dx;
                double py = box->miny + (y + jy) * dy;
                hc += hit_scene(s, px, py);
            }
            double p = (double)hc / n;
            buf[idx] = (unsigned char)floor(255.0 * p + 0.5);
            /* 实测区间全宽：方差用最终样本无偏估计，取全图最大 */
            double s2n = (double)hc * (n - hc) / ((double)n * (n - 1 > 0 ? n - 1 : 1));
            double wd = 2.0 * z * sqrt(s2n / n);
            if (wd > width_obs) width_obs = wd;
        }

    rep->width_pred = 2.0 * z * sqrt(var_max / (double)n);
    rep->width_obs  = degenerate ? 0.0 : width_obs;
    rep->status     = RADAPT_OK;

    clock_gettime(CLOCK_MONOTONIC, &t1);
    rep->ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
              (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
    free(hits);
    return RADAPT_OK;
}
