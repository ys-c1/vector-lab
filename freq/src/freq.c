#define _POSIX_C_SOURCE 200809L
#include "freq.h"
#include "fft.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------- 计时 ---------------- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e3 + (double)ts.tv_nsec * 1e-6;
}

/* ---------------- 小工具 ---------------- */

static int next_pow2(int v) {
    int p = 1;
    while (p < v) p <<= 1;
    return p;
}

/* reflect-101：-1 -> 1，n -> n-2；用于所有边界延拓 */
static int reflect101(int i, int n) {
    if (n <= 1) return 0;
    while (i < 0 || i >= n) {
        if (i < 0) i = -i;
        if (i >= n) i = 2 * n - 2 - i;
    }
    return i;
}

static unsigned char clip_u8(double v) {
    if (!(v > 0.0)) return 0;          /* 同时吸收 NaN */
    if (v >= 255.0) return 255;
    return (unsigned char)floor(v + 0.5);
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x < y) ? -1 : (x > y) ? 1 : 0;
}

void fq_options_default(FqOptions *o) {
    if (!o) return;
    o->use_freq = 1;
    o->sigma = 1.0;
    o->noise_mult = 8.0;
    o->cutoff_max = FQ_CUTOFF_MAX;
}

const char *fq_strerror(int code) {
    switch (code) {
    case FQ_OK: return "ok";
    case FQ_E_ARG: return "null argument";
    case FQ_E_DIM: return "width/height out of range";
    case FQ_E_MEM: return "out of memory";
    case FQ_E_CUTOFF_ZERO: return "cutoff frequency is zero (no significant band)";
    case FQ_E_CUTOFF_HIGH: return "cutoff frequency exceeds the upper limit";
    case FQ_E_DISABLED: return "frequency-domain path disabled at compile time";
    default: return "unknown error";
    }
}

static int check_dims(int w, int h) {
    if (w <= 1 || h <= 1) return FQ_E_DIM;
    int n = next_pow2(w > h ? w : h);
    if (n > FQ_MAX_FFT) return FQ_E_DIM;
    return FQ_OK;
}

/* ---------------- 频域工作区 ---------------- */

typedef struct {
    int n;              /* 填充后边长（2 的幂） */
    int padx, pady;     /* 原图在填充图中的偏移 */
    double mean;        /* 原图均值 */
    double *re, *im;    /* n*n 频谱 */
    FqPlan plan;
    int plan_ok;
} FqWork;

static void work_free(FqWork *wk) {
    if (!wk) return;
    if (wk->plan_ok) fq_plan_free(&wk->plan);
    free(wk->re);
    free(wk->im);
    wk->re = wk->im = NULL;
    wk->plan_ok = 0;
}

/* 反射填充到 n×n 并去均值；返回填充缓冲区（调用方 free） */
static double *prepare_padded(const unsigned char *in, int w, int h,
                              int n, int padx, int pady, double *mean_out) {
    size_t total = (size_t)w * (size_t)h;
    double sum = 0.0;
    for (size_t i = 0; i < total; ++i) sum += in[i];
    double mean = total ? sum / (double)total : 0.0;
    if (mean_out) *mean_out = mean;

    double *buf = (double*)malloc(sizeof(double) * (size_t)n * (size_t)n);
    if (!buf) return NULL;
    for (int y = 0; y < n; ++y) {
        int sy = reflect101(y - pady, h);
        for (int x = 0; x < n; ++x) {
            int sx = reflect101(x - padx, w);
            buf[(size_t)y * n + x] = (double)in[(size_t)sy * w + sx] - mean;
        }
    }
    return buf;
}

/* 准备 + 正变换；pad_ms / fwd_ms 可为 NULL。 */
static int work_prepare(FqWork *wk, const unsigned char *in, int w, int h,
                        double *pad_ms, double *fwd_ms) {
    memset(wk, 0, sizeof *wk);
    int n = next_pow2(w > h ? w : h);
    wk->n = n;
    wk->padx = (n - w) / 2;
    wk->pady = (n - h) / 2;

    double t0 = now_ms();
    double *buf = prepare_padded(in, w, h, n, wk->padx, wk->pady, &wk->mean);
    if (!buf) return FQ_E_MEM;

    size_t total = (size_t)n * (size_t)n;
    wk->re = (double*)malloc(sizeof(double) * total);
    wk->im = (double*)malloc(sizeof(double) * total);
    if (!wk->re || !wk->im) { free(buf); work_free(wk); return FQ_E_MEM; }
    memcpy(wk->re, buf, sizeof(double) * total);
    memset(wk->im, 0, sizeof(double) * total);
    if (fq_plan_init(&wk->plan, n) != 0) { free(buf); work_free(wk); return FQ_E_MEM; }
    wk->plan_ok = 1;
    free(buf);
    double t1 = now_ms();

    fq_fft_2d(&wk->plan, wk->re, wk->im, 0);
    double t2 = now_ms();

    if (pad_ms) *pad_ms = t1 - t0;
    if (fwd_ms) *fwd_ms = t2 - t1;
    return FQ_OK;
}

/* 环带统计：返回每环带总功率（power）与平均功率（mean）以及 AC 总功率。
 * step = 1/n，单位 cycles/pixel；只看 r <= FQ_CUTOFF_MAX 的环带。
 * 返回环带数（含 DC），失败返回 -1。 */
static int radial_stats(const FqWork *wk, double **mean_out, double **power_out,
                        double *ac_sum_out) {
    int n = wk->n;
    double step = 1.0 / (double)n;
    int nbins = (int)floor(FQ_CUTOFF_MAX / step + 0.5) + 1;

    double *pow_ = (double*)calloc((size_t)nbins, sizeof(double));
    double *cnt = (double*)calloc((size_t)nbins, sizeof(double));
    double *mean = (double*)calloc((size_t)nbins, sizeof(double));
    if (!pow_ || !cnt || !mean) { free(pow_); free(cnt); free(mean); return -1; }

    for (int y = 0; y < n; ++y) {
        double fy = (y <= n / 2) ? (double)y / n : (double)(y - n) / n;
        for (int x = 0; x < n; ++x) {
            double fx = (x <= n / 2) ? (double)x / n : (double)(x - n) / n;
            double r = sqrt(fx * fx + fy * fy);
            int b = (int)floor(r / step + 0.5);
            if (b >= nbins) b = nbins - 1;
            double re = wk->re[(size_t)y * n + x];
            double im = wk->im[(size_t)y * n + x];
            pow_[b] += re * re + im * im;
            cnt[b] += 1.0;
        }
    }
    double ac = 0.0;
    for (int b = 0; b < nbins; ++b) {
        mean[b] = cnt[b] > 0 ? pow_[b] / cnt[b] : 0.0;
        if (b > 0) ac += pow_[b];
    }
    free(cnt);
    *mean_out = mean;
    if (power_out) *power_out = pow_; else free(pow_);
    *ac_sum_out = ac;
    return nbins;
}

/* 按环带能量分布选截止。
 * 0 成功；-1 纯色（无 AC）；-2 截止为零；-3 超上限；-4 内存。 */
static int select_cutoff(const double *mean, const double *power, int nbins, int n,
                         const FqOptions *opt, FqReport *rep,
                         double *cutoff_out, double *retained_out) {
    double step = 1.0 / (double)n;
    int m = nbins - 1;
    if (m <= 0) return -1;

    double *sorted = (double*)malloc(sizeof(double) * (size_t)m);
    if (!sorted) return -4;
    for (int b = 1; b < nbins; ++b) sorted[b - 1] = mean[b];
    qsort(sorted, (size_t)m, sizeof(double), cmp_double);
    double floor_med = (m & 1) ? sorted[m / 2] : 0.5 * (sorted[m / 2 - 1] + sorted[m / 2]);
    free(sorted);

    double peak = 0.0, ac = 0.0;
    for (int b = 1; b < nbins; ++b) {
        if (mean[b] > peak) peak = mean[b];
        ac += power ? power[b] : mean[b];   /* 保留比例按总功率算，不能混用均值 */
    }

    double mult = opt->noise_mult > 0.0 ? opt->noise_mult : 8.0;
    double threshold = mult * floor_med;
    double guard = 1e-12 * peak;
    if (threshold < guard) threshold = guard;

    int last_sig = 0, sig_count = 0;
    for (int b = 1; b < nbins; ++b)
        if (mean[b] >= threshold) { last_sig = b; ++sig_count; }

    rep->noise_floor = floor_med;
    rep->peak = peak;
    rep->threshold = threshold;
    rep->sig_bins = sig_count;

    if (last_sig == 0) {
        snprintf(rep->reason, sizeof rep->reason,
                 "截止频率为零：%d 个 AC 环带均低于显著门限 %.3e"
                 "（底噪中位数 %.3e × %.2f，峰值 %.3e），无可用纹理基频，拒绝",
                 m, threshold, floor_med, mult, peak);
        return -2;
    }

    double cutoff = ((double)last_sig + 0.5) * step;
    double limit = opt->cutoff_max;
    if (!(limit > 0.0) || limit > FQ_CUTOFF_MAX) limit = FQ_CUTOFF_MAX;
    if (cutoff > limit) {
        *cutoff_out = cutoff;          /* 拒绝时也要回带实测值 */
        snprintf(rep->reason, sizeof rep->reason,
                 "截止频率 %.4f cycles/px 超过上限 %.4f（奈奎斯特对角线），拒绝",
                 cutoff, limit);
        return -3;
    }

    double keep = 0.0;
    if (power) {
        for (int b = 1; b <= last_sig; ++b) keep += power[b];
    } else {
        for (int b = 1; b <= last_sig; ++b) keep += mean[b];
    }
    *cutoff_out = cutoff;
    *retained_out = ac > 0.0 ? keep / ac : 0.0;
    return 0;
}

/* 升余弦低通；feather 为过渡宽度（一个环带）。 */
static double lowpass_gain(double r, double cutoff, double feather) {
    if (r <= cutoff) return 1.0;
    if (r >= cutoff + feather) return 0.0;
    double t = (r - cutoff) / feather;
    return 0.5 * (1.0 + cos(M_PI * t));
}

/* 施加低通后逆变换；返回 AC 预测保留比例（不含 DC）。 */
static double apply_lowpass(FqWork *wk, double cutoff) {
    int n = wk->n;
    double step = 1.0 / (double)n;
    double feather = step;

    double pred = 0.0, total = 0.0;
    for (int y = 0; y < n; ++y) {
        double fy = (y <= n / 2) ? (double)y / n : (double)(y - n) / n;
        for (int x = 0; x < n; ++x) {
            double fx = (x <= n / 2) ? (double)x / n : (double)(x - n) / n;
            double r = sqrt(fx * fx + fy * fy);
            double h = lowpass_gain(r, cutoff, feather);
            size_t i = (size_t)y * n + x;
            if (i != 0) {
                double p = wk->re[i] * wk->re[i] + wk->im[i] * wk->im[i];
                total += p;
                pred += p * h * h;
            }
            wk->re[i] *= h;
            wk->im[i] *= h;
        }
    }
    fq_fft_2d(&wk->plan, wk->re, wk->im, 1);
    return total > 0.0 ? pred / total : 0.0;
}

/* 从逆变换结果裁剪量化；返回 实测 AC 保留比例（不含 DC）。 */
static double assemble(const FqWork *wk, int w, int h,
                       const unsigned char *in, unsigned char *out) {
    int n = wk->n;
    size_t total = (size_t)w * (size_t)h;
    double mean_in = 0.0, mean_out = 0.0;
    for (size_t i = 0; i < total; ++i) mean_in += in[i];
    mean_in /= (double)total;

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double v = wk->re[(size_t)(y + wk->pady) * n + (x + wk->padx)] + wk->mean;
            out[(size_t)y * w + x] = clip_u8(v);
        }

    for (size_t i = 0; i < total; ++i) mean_out += out[i];
    mean_out /= (double)total;

    double ac_in = 0.0, ac_out = 0.0;
    for (size_t i = 0; i < total; ++i) {
        double a = (double)in[i] - mean_in;
        double b = (double)out[i] - mean_out;
        ac_in += a * a;
        ac_out += b * b;
    }
    return ac_in > 0.0 ? ac_out / ac_in : 0.0;
}

static void copy_out(const unsigned char *in, int w, int h, unsigned char *out) {
    if (out != in) memcpy(out, in, (size_t)w * (size_t)h);
}

/* ---------------- 公共接口 ---------------- */

int fq_choose_cutoff(const unsigned char *in, int w, int h,
                     const FqOptions *opt, FqReport *rep) {
    if (!rep) return FQ_E_ARG;
    memset(rep, 0, sizeof *rep);
    if (!in) return FQ_E_ARG;
    int chk = check_dims(w, h);
    if (chk != FQ_OK) return chk;
    if (!FQ_ENABLE_FREQ) return FQ_E_DISABLED;

    FqOptions local;
    if (opt) local = *opt; else fq_options_default(&local);

    double t0 = now_ms();
    FqWork wk;
    int rc = work_prepare(&wk, in, w, h, &rep->ms_pad, &rep->ms_forward);
    if (rc != FQ_OK) return rc;
    rep->nfft = wk.n;
    rep->ms_prep = rep->ms_pad + rep->ms_forward;   /* 准备 = 填充 + 正变换 */

    double t2 = now_ms();
    double *mean = NULL, *power = NULL, ac = 0.0;
    int nbins = radial_stats(&wk, &mean, &power, &ac);
    if (nbins < 0 || !mean) { work_free(&wk); return FQ_E_MEM; }

    if (ac <= 0.0) {
        rep->status = FQ_ST_FLAT;
        rep->flat = 1;
        snprintf(rep->reason, sizeof rep->reason,
                 "纯色图：去均值后 AC 能量为零，无截止频率可定（单独报出，不做滤波）");
        free(mean); free(power); work_free(&wk);
        rep->ms_total = now_ms() - t0;
        return FQ_OK;
    }

    double cutoff = 0.0, retained = 0.0;
    int sel = select_cutoff(mean, power, nbins, wk.n, &local, rep, &cutoff, &retained);
    free(mean); free(power);
    rep->ms_select = now_ms() - t2;
    work_free(&wk);
    rep->ms_total = now_ms() - t0;

    if (sel == -4) return FQ_E_MEM;
    if (sel == -1) { rep->status = FQ_ST_FLAT; rep->flat = 1; return FQ_OK; }
    if (sel == -2) { rep->status = FQ_ST_REJ_ZERO; rep->cutoff = 0.0; return FQ_E_CUTOFF_ZERO; }
    if (sel == -3) { rep->status = FQ_ST_REJ_HIGH; rep->cutoff = cutoff; return FQ_E_CUTOFF_HIGH; }

    rep->status = FQ_ST_OK;
    rep->cutoff = cutoff;
    rep->retained = retained;
    return FQ_OK;
}

int fq_filter_freq(const unsigned char *in, int w, int h, double cutoff,
                   unsigned char *out, double *retained, double *retained_meas) {
    if (!in || !out) return FQ_E_ARG;
    int chk = check_dims(w, h);
    if (chk != FQ_OK) return chk;
    if (!FQ_ENABLE_FREQ) return FQ_E_DISABLED;
    if (!(cutoff > 0.0)) return FQ_E_CUTOFF_ZERO;

    FqWork wk;
    int rc = work_prepare(&wk, in, w, h, NULL, NULL);
    if (rc != FQ_OK) return rc;
    double pred = apply_lowpass(&wk, cutoff);
    double meas = assemble(&wk, w, h, in, out);
    if (retained) *retained = pred;
    if (retained_meas) *retained_meas = meas;
    work_free(&wk);
    return FQ_OK;
}

int fq_run(const unsigned char *in, int w, int h, const FqOptions *opt,
           unsigned char *out, FqReport *rep) {
    if (!rep) return FQ_E_ARG;
    memset(rep, 0, sizeof *rep);
    if (!in || !out) return FQ_E_ARG;
    int chk = check_dims(w, h);
    if (chk != FQ_OK) return chk;

    FqOptions local;
    if (opt) local = *opt; else fq_options_default(&local);

    double t_start = now_ms();

    if (!local.use_freq) {
        /* 原有空域路径：不做任何频域分析 */
        int rc = fq_filter_spatial(in, w, h, local.sigma, out);
        rep->status = FQ_ST_OK;
        rep->ms_total = now_ms() - t_start;
        return rc;
    }
    if (!FQ_ENABLE_FREQ) {
        copy_out(in, w, h, out);
        rep->status = FQ_ST_OK;
        snprintf(rep->reason, sizeof rep->reason,
                 "编译期关闭频域逻辑（FQ_ENABLE_FREQ=0），输出为输入副本");
        rep->ms_total = now_ms() - t_start;
        return FQ_E_DISABLED;
    }

    FqWork wk;
    int rc = work_prepare(&wk, in, w, h, &rep->ms_pad, &rep->ms_forward);
    if (rc != FQ_OK) return rc;
    rep->nfft = wk.n;
    rep->ms_prep = rep->ms_pad + rep->ms_forward;   /* 准备 = 填充 + 正变换 */

    /* 反变换验证：不滤波直接往返，记录最大绝对误差 */
    {
        double t0 = now_ms();
        /* 注意：wk.re/wk.im 在 verify 之后还要用于选截止与滤波，
         * 因此这里复制一份单独逆变换，绝不原地改谱。 */
        double *pin = prepare_padded(in, w, h, wk.n, wk.padx, wk.pady, NULL);
        double *sre = NULL, *sim = NULL;
        size_t total = (size_t)wk.n * wk.n;
        if (pin) {
            sre = (double*)malloc(sizeof(double) * total);
            sim = (double*)malloc(sizeof(double) * total);
        }
        if (pin && sre && sim) {
            memcpy(sre, wk.re, sizeof(double) * total);
            memcpy(sim, wk.im, sizeof(double) * total);   /* 频谱含虚部，必须整体复制 */
            fq_fft_2d(&wk.plan, sre, sim, 1);
            double err = 0.0;
            for (size_t i = 0; i < total; ++i) {
                double d = fabs(sre[i] - pin[i]);
                if (d > err) err = d;
            }
            rep->roundtrip_err = err;
        }
        free(pin); free(sre); free(sim);
        rep->ms_verify = now_ms() - t0;
    }

    double t2 = now_ms();
    double *mean = NULL, *power = NULL, ac = 0.0;
    int nbins = radial_stats(&wk, &mean, &power, &ac);
    if (nbins < 0 || !mean) { work_free(&wk); return FQ_E_MEM; }

    if (ac <= 0.0) {
        rep->status = FQ_ST_FLAT;
        rep->flat = 1;
        snprintf(rep->reason, sizeof rep->reason,
                 "纯色图：去均值后 AC 能量为零，无截止频率可定（单独报出，不做滤波）");
        free(mean); free(power); work_free(&wk);
        copy_out(in, w, h, out);
        rep->ms_total = now_ms() - t_start;
        return FQ_OK;
    }

    double cutoff = 0.0, retained = 0.0;
    int sel = select_cutoff(mean, power, nbins, wk.n, &local, rep, &cutoff, &retained);
    free(mean); free(power);
    rep->ms_select = now_ms() - t2;
    rep->retained = retained;

    if (sel == -1) {
        rep->status = FQ_ST_FLAT; rep->flat = 1;
        work_free(&wk); copy_out(in, w, h, out);
        rep->ms_total = now_ms() - t_start;
        return FQ_OK;
    }
    if (sel == -2) {
        rep->status = FQ_ST_REJ_ZERO; rep->cutoff = 0.0;
        work_free(&wk); copy_out(in, w, h, out);
        rep->ms_total = now_ms() - t_start;
        return FQ_E_CUTOFF_ZERO;
    }
    if (sel == -3) {
        rep->status = FQ_ST_REJ_HIGH; rep->cutoff = cutoff;
        work_free(&wk); copy_out(in, w, h, out);
        rep->ms_total = now_ms() - t_start;
        return FQ_E_CUTOFF_HIGH;
    }
    if (sel == -4) { work_free(&wk); return FQ_E_MEM; }

    rep->status = FQ_ST_OK;
    rep->cutoff = cutoff;

    double t3 = now_ms();
    double pred = apply_lowpass(&wk, cutoff);
    rep->ms_inverse = now_ms() - t3;

    double t4 = now_ms();
    double meas = assemble(&wk, w, h, in, out);
    rep->ms_assemble = now_ms() - t4;
    work_free(&wk);

    /* 预测与实测取较小者会更保守；这里两者都报，主口径用实测 */
    if (retained <= 0.0) rep->retained = pred;
    rep->retained_meas = meas;
    rep->ms_total = now_ms() - t_start;
    return FQ_OK;
}

/* ---------------- 空域兼容路径（固定点，逐字节可复现） ---------------- */

static void spatial_kernel(double sigma, int *r_out, unsigned *q) {
    int r = (int)ceil(3.0 * sigma);
    if (r < 1) r = 1;
    if (r > FQ_SPATIAL_MAX_R) r = FQ_SPATIAL_MAX_R;
    double w[2 * FQ_SPATIAL_MAX_R + 1];
    double sum = 0.0;
    for (int k = -r; k <= r; ++k) {
        double v = exp(-(double)(k * k) / (2.0 * sigma * sigma));
        w[k + r] = v;
        sum += v;
    }
    unsigned acc = 0;
    for (int k = 0; k <= 2 * r; ++k) {
        q[k] = (unsigned)floor(w[k] / sum * 65536.0 + 0.5);
        acc += q[k];
    }
    long long diff = 65536LL - (long long)acc;
    q[r] = (unsigned)((long long)q[r] + diff);   /* 残差补中心，权重和恰为 65536 */
    *r_out = r;
}

int fq_filter_spatial(const unsigned char *in, int w, int h, double sigma,
                      unsigned char *out) {
    if (!in || !out) return FQ_E_ARG;
    int chk = check_dims(w, h);
    if (chk != FQ_OK) return chk;

    if (!(sigma > 1e-6)) {           /* 逐字节空操作 */
        if (out != in) memcpy(out, in, (size_t)w * (size_t)h);
        return FQ_OK;
    }

    int r;
    unsigned q[2 * FQ_SPATIAL_MAX_R + 1];
    spatial_kernel(sigma, &r, q);

    size_t total = (size_t)w * (size_t)h;
    long long *hbuf = (long long*)malloc(sizeof(long long) * total);
    if (!hbuf) return FQ_E_MEM;

    for (int y = 0; y < h; ++y) {
        const unsigned char *row = in + (size_t)y * w;
        long long *dst = hbuf + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            long long acc = 0;
            for (int k = -r; k <= r; ++k)
                acc += (long long)q[k + r] * (long long)row[reflect101(x + k, w)];
            dst[x] = acc;
        }
    }
    for (int y = 0; y < h; ++y) {
        unsigned char *dst = out + (size_t)y * w;
        for (int x = 0; x < w; ++x) {
            long long acc = 0;
            for (int k = -r; k <= r; ++k)
                acc += (long long)q[k + r] * hbuf[(size_t)reflect101(y + k, h) * w + x];
            long long v = (acc + (1LL << 31)) >> 32;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            dst[x] = (unsigned char)v;
        }
    }
    free(hbuf);
    return FQ_OK;
}

/* ---------------- 验证 / 度量 ---------------- */

double fq_roundtrip_err(const unsigned char *in, int w, int h) {
    if (!in || check_dims(w, h) != FQ_OK) return -1.0;
    if (!FQ_ENABLE_FREQ) return -1.0;
    FqWork wk;
    if (work_prepare(&wk, in, w, h, NULL, NULL) != FQ_OK) return -1.0;
    double *pin = prepare_padded(in, w, h, wk.n, wk.padx, wk.pady, NULL);
    if (!pin) { work_free(&wk); return -1.0; }
    fq_fft_2d(&wk.plan, wk.re, wk.im, 1);
    double err = 0.0;
    size_t total = (size_t)wk.n * wk.n;
    for (size_t i = 0; i < total; ++i) {
        double d = fabs(wk.re[i] - pin[i]);
        if (d > err) err = d;
    }
    free(pin);
    work_free(&wk);
    return err;
}

/* 把一张 8 位图转成填充谱 */
static int spectrum_of(const unsigned char *img, int w, int h, int n,
                       int padx, int pady, FqPlan *plan,
                       double *re, double *im) {
    double *buf = prepare_padded(img, w, h, n, padx, pady, NULL);
    if (!buf) return -1;
    memcpy(re, buf, sizeof(double) * (size_t)n * n);
    memset(im, 0, sizeof(double) * (size_t)n * n);
    free(buf);
    fq_fft_2d(plan, re, im, 0);
    return 0;
}

double fq_blur_degree(const unsigned char *out, const unsigned char *ref,
                      int w, int h) {
    if (!out || !ref || check_dims(w, h) != FQ_OK) return -1.0;
    if (!FQ_ENABLE_FREQ) return -1.0;

    int n = next_pow2(w > h ? w : h);
    int padx = (n - w) / 2, pady = (n - h) / 2;
    FqPlan plan;
    if (fq_plan_init(&plan, n) != 0) return -1.0;
    size_t total = (size_t)n * n;
    double *re = (double*)malloc(sizeof(double) * total);
    double *im = (double*)malloc(sizeof(double) * total);
    double *ore = (double*)malloc(sizeof(double) * total);
    double *oim = (double*)malloc(sizeof(double) * total);
    if (!re || !im || !ore || !oim) {
        free(re); free(im); free(ore); free(oim); fq_plan_free(&plan);
        return -1.0;
    }
    if (spectrum_of(ref, w, h, n, padx, pady, &plan, re, im) != 0 ||
        spectrum_of(out, w, h, n, padx, pady, &plan, ore, oim) != 0) {
        free(re); free(im); free(ore); free(oim); fq_plan_free(&plan);
        return -1.0;
    }

    double num = 0.0, den = 0.0;
    for (size_t i = 1; i < total; ++i) {   /* 跳过 DC */
        num += ore[i] * re[i] + oim[i] * im[i];
        den += re[i] * re[i] + im[i] * im[i];
    }
    free(re); free(im); free(ore); free(oim);
    fq_plan_free(&plan);
    if (den <= 0.0) return 0.0;
    double d = num / den;
    if (d > 1.0) d = 1.0;
    if (d < 0.0) d = 0.0;
    return 1.0 - d;
}

double fq_hf_removed(const unsigned char *out, const unsigned char *in,
                     int w, int h, double cutoff) {
    if (!out || !in || check_dims(w, h) != FQ_OK) return -1.0;
    if (!FQ_ENABLE_FREQ) return -1.0;

    int n = next_pow2(w > h ? w : h);
    int padx = (n - w) / 2, pady = (n - h) / 2;
    FqPlan plan;
    if (fq_plan_init(&plan, n) != 0) return -1.0;
    size_t total = (size_t)n * n;
    double *re = (double*)malloc(sizeof(double) * total);
    double *im = (double*)malloc(sizeof(double) * total);
    if (!re || !im) { free(re); free(im); fq_plan_free(&plan); return -1.0; }

    double ein = 0.0, eout = 0.0;
    for (int pass = 0; pass < 2; ++pass) {
        if (spectrum_of(pass == 0 ? in : out, w, h, n, padx, pady, &plan, re, im) != 0) {
            free(re); free(im); fq_plan_free(&plan);
            return -1.0;
        }
        for (int y = 0; y < n; ++y) {
            double fy = (y <= n / 2) ? (double)y / n : (double)(y - n) / n;
            for (int x = 0; x < n; ++x) {
                double fx = (x <= n / 2) ? (double)x / n : (double)(x - n) / n;
                if (sqrt(fx * fx + fy * fy) <= cutoff) continue;
                size_t i = (size_t)y * n + x;
                double p = re[i] * re[i] + im[i] * im[i];
                if (pass == 0) ein += p; else eout += p;
            }
        }
    }
    free(re); free(im);
    fq_plan_free(&plan);
    if (ein <= 0.0) return 0.0;
    double r = 1.0 - eout / ein;
    return r < 0.0 ? 0.0 : (r > 1.0 ? 1.0 : r);
}
