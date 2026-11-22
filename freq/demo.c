/* 四类细密纹理的频域定截止 vs 空域滤波对照。
 *
 * 每类图：
 *   - 按能量分布选截止，给出截止频率、保留能量比例、耗时；
 *   - 弱化两成看模糊度（以真值纹理为参照）是否达标；
 *   - 输出 clean/noisy/freq/spatial 四张 PGM 便于肉眼复核；
 *   - 纯色与两种拒绝用例单独一段。
 *
 * 输出为 Markdown 表格，可直接贴进 README。
 */
#include "freq.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rng_state = 12345u;
static void rng_seed(unsigned s) { rng_state = s ? s : 1u; }
static unsigned rng_u32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

enum { T_STRIPES = 0, T_CHECKER, T_DIAG, T_DOTS, T_CORNER, T_FLAT };

static void synth(unsigned char *buf, int w, int h, int kind) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v;
            switch (kind) {
            case T_STRIPES: v = ((x % 4) < 2) ? 216 : 40; break;
            case T_CHECKER: v = ((((x % 4) < 2) ^ ((y % 4) < 2))) ? 216 : 40; break;
            case T_DIAG:    v = (((x + y) % 4) < 2) ? 216 : 40; break;
            case T_DOTS: {
                int dx2 = 2 * (x % 8) - 7, dy2 = 2 * (y % 8) - 7;
                v = (dx2 * dx2 + dy2 * dy2 <= 24) ? 216 : 40;
                break;
            }
            case T_CORNER:  v = (((x + y) % 2) == 0) ? 216 : 40; break;
            default:        v = 128; break;
            }
            buf[(size_t)y * w + x] = (unsigned char)v;
        }
}

static void add_noise(unsigned char *buf, int w, int h, int amp, unsigned seed) {
    rng_seed(seed);
    for (size_t i = 0; i < (size_t)w * h; ++i) {
        int n = (int)((rng_u32() >> 16) % 256u);
        n = (n * amp) / 256 - amp / 2;
        int v = buf[i] + n;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        buf[i] = (unsigned char)v;
    }
}

static int save_pgm(const char *path, const unsigned char *buf, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * h, f);
    fclose(f);
    return 0;
}

/* 空域基线：在所有 sigma 中，取“高频去除量不低于频域方案”的最小 sigma；
 * 若最大 sigma 仍追不上去噪量，取最大 sigma。返回该档的模糊度。 */
static double spatial_baseline(const unsigned char *in, const unsigned char *clean,
                               int w, int h, double cutoff, double hf_freq,
                               unsigned char *sp, double *sig_out) {
    const double s0 = 0.2, s1 = 3.0, ds = 0.1;
    double best = 2.0, best_sig = s1, max_hf = -1.0, max_blur = 2.0, max_sig = s0;
    for (double sig = s0; sig <= s1 + 1e-9; sig += ds) {
        fq_filter_spatial(in, w, h, sig, sp);
        double b = fq_blur_degree(sp, clean, w, h);
        double hf = fq_hf_removed(sp, in, w, h, cutoff);
        if (hf > max_hf) { max_hf = hf; max_blur = b; max_sig = sig; }
        if (hf >= hf_freq - 0.01 && b < best) { best = b; best_sig = sig; }
    }
    if (best > 1.5) { best = max_blur; best_sig = max_sig; }
    if (sig_out) *sig_out = best_sig;
    return best;
}

int main(void) {
    const int w = 128, h = 128;
    const char *names[4] = {"细条纹", "棋盘", "斜纹", "点阵"};
    const int kinds[4] = {T_STRIPES, T_CHECKER, T_DIAG, T_DOTS};
    unsigned char *clean = (unsigned char*)malloc((size_t)w * h);
    unsigned char *in = (unsigned char*)malloc((size_t)w * h);
    unsigned char *out = (unsigned char*)malloc((size_t)w * h);
    unsigned char *sp = (unsigned char*)malloc((size_t)w * h);
    if (!clean || !in || !out || !sp) return 1;

    FqOptions opt;
    fq_options_default(&opt);

    printf("# 频域定截止 vs 空域滤波（%dx%d）\n\n", w, h);
    printf("| 纹理 | 截止频率 (cyc/px) | 保留能量 | 耗时 (ms) | 模糊度(频域) | "
           "空域基线 σ | 模糊度(空域) | 降幅 |\n");
    printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");

    for (int k = 0; k < 4; ++k) {
        synth(clean, w, h, kinds[k]);
        memcpy(in, clean, (size_t)w * h);
        add_noise(in, w, h, 24, 900u + (unsigned)k);

        /* 耗时取 5 次中位数，输出取最后一次 */
        FqReport rep;
        double ms[5];
        int rc = FQ_OK;
        for (int t = 0; t < 5; ++t) {
            rc = fq_run(in, w, h, &opt, out, &rep);
            ms[t] = rep.ms_total;
        }
        for (int a = 0; a < 5; ++a)
            for (int b = a + 1; b < 5; ++b)
                if (ms[b] < ms[a]) { double tmp = ms[a]; ms[a] = ms[b]; ms[b] = tmp; }
        double ms_med = ms[2];
        double bf = -1.0, bs = -1.0, sig = 0.0;
        if (rc == FQ_OK) {
            bf = fq_blur_degree(out, clean, w, h);
            double hf = fq_hf_removed(out, in, w, h, rep.cutoff);
            bs = spatial_baseline(in, clean, w, h, rep.cutoff, hf, sp, &sig);
        }
        printf("| %s | %.4f | %.2f%% | %.2f | %.4f | %.1f | %.4f | %.1f%% |\n",
               names[k], rep.cutoff, rep.retained * 100.0, ms_med,
               bf, sig, bs, bs > 1e-9 ? (1.0 - bf / bs) * 100.0 : 0.0);

        char path[64];
        snprintf(path, sizeof path, "demo_%d_clean.pgm", k);
        save_pgm(path, clean, w, h);
        snprintf(path, sizeof path, "demo_%d_noisy.pgm", k);
        save_pgm(path, in, w, h);
        snprintf(path, sizeof path, "demo_%d_freq.pgm", k);
        save_pgm(path, out, w, h);
        snprintf(path, sizeof path, "demo_%d_spatial.pgm", k);
        save_pgm(path, sp, w, h);
    }

    printf("\n## 特殊输入\n\n");
    printf("| 输入 | 结果 | 截止 | 说明 |\n|---|---|---:|---|\n");

    /* 纯色 */
    memset(in, 128, (size_t)w * h);
    FqReport rep;
    int rc = fq_run(in, w, h, &opt, out, &rep);
    const char *flat_tag = (rc == FQ_E_DISABLED) ? "频域已关闭"
                         : rep.flat ? "纯色（单独报出）" : "异常";
    printf("| 纯色 | %s | %.4f | %s |\n", flat_tag, rep.cutoff, rep.reason);
    save_pgm("demo_flat_out.pgm", out, w, h);
    (void)rc;

    /* 纯色但有三点微差（仍无显著 AC） */
    memset(in, 128, (size_t)w * h);
    in[0] = 129; in[w + 3] = 127;
    rc = fq_run(in, w, h, &opt, out, &rep);
    printf("| 近纯色 | %s | %.4f | %s |\n",
           rc == FQ_E_DISABLED ? "频域已关闭"
           : rep.status == FQ_ST_REJ_ZERO ? "拒绝（截止为零）" : "通过",
           rep.cutoff, rep.reason);

    /* 角点交替：截止超上限 */
    synth(in, w, h, T_CORNER);
    rc = fq_run(in, w, h, &opt, out, &rep);
    printf("| 角点交替 | %s | %.4f | %s |\n",
           rc == FQ_E_CUTOFF_HIGH ? "拒绝（超上限）"
           : rc == FQ_E_DISABLED ? "频域已关闭" : "通过",
           rep.cutoff, rep.reason);

    /* 白噪声：无显著环带 */
    rng_seed(4242);
    for (size_t i = 0; i < (size_t)w * h; ++i) in[i] = (unsigned char)((rng_u32() >> 16) & 0xffu);
    rc = fq_run(in, w, h, &opt, out, &rep);
    printf("| 白噪声 | %s | %.4f | %s |\n",
           rc == FQ_E_CUTOFF_ZERO ? "拒绝（截止为零）"
           : rc == FQ_E_DISABLED ? "频域已关闭" : "通过",
           rep.cutoff, rep.reason);

    /* 关闭新逻辑：空域逐字节对照 */
    synth(in, w, h, T_CHECKER);
    add_noise(in, w, h, 24, 777u);
    FqOptions off;
    fq_options_default(&off);
    off.use_freq = 0;
    off.sigma = 1.0;
    fq_run(in, w, h, &off, out, &rep);
    fq_filter_spatial(in, w, h, 1.0, sp);
    printf("\n关闭新逻辑（use_freq=0, sigma=1.0）：与空域路径逐字节%s\n",
           memcmp(out, sp, (size_t)w * h) == 0 ? "一致" : "不一致（错误）");

    free(clean); free(in); free(out); free(sp);
    return 0;
}
