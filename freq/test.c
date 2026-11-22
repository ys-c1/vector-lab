#include "freq.h"
#include "fft.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)
#define NEAR(a,b,t) (fabs((double)(a)-(double)(b)) <= (t))

/* ---------- 确定性 LCG，与任何库无关 ---------- */
static unsigned rng_state = 12345u;
static void rng_seed(unsigned s) { rng_state = s ? s : 1u; }
static unsigned rng_u32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

/* ---------- 合成纹理（四类 + 纯色/噪声/角点） ---------- */
enum { T_STRIPES = 0, T_CHECKER, T_DIAG, T_DOTS, T_FLAT, T_CORNER, T_WHITE, T_COUNT };

static void synth(unsigned char *buf, int w, int h, int kind, int noise_amp) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v;
            switch (kind) {
            case T_STRIPES: v = ((x % 4) < 2) ? 216 : 40; break;
            case T_CHECKER: v = ((((x % 4) < 2) ^ ((y % 4) < 2))) ? 216 : 40; break;
            case T_DIAG:    v = (((x + y) % 4) < 2) ? 216 : 40; break;
            case T_DOTS: {
                /* 8×8 周期内中心在格心（半整数），频谱对称且落在对角线以内 */
                int dx2 = 2 * (x % 8) - 7, dy2 = 2 * (y % 8) - 7;
                v = (dx2 * dx2 + dy2 * dy2 <= 24) ? 216 : 40;
                break;
            }
            case T_FLAT:    v = 128; break;
            case T_CORNER:  v = (((x + y) % 2) == 0) ? 216 : 40; break;
            default:        v = 128; break;
            }
            if (noise_amp > 0) {
                int n = (int)((rng_u32() >> 16) % 256u);
                n = (n * noise_amp) / 256 - noise_amp / 2;
                v += n;
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            buf[(size_t)y * w + x] = (unsigned char)v;
        }
}

/* ---------- FFT 单元检查 ---------- */
static void test_fft(void) {
    FqPlan p;
    CK(fq_plan_init(&p, 8) == 0);
    double re[8] = {1,1,1,1,1,1,1,1}, im[8] = {0,0,0,0,0,0,0,0};
    fq_fft_1d(&p, re, im, 1, 0);
    /* 常数序列：只有 DC = 8 */
    CK(NEAR(re[0], 8.0, 1e-9));
    int others_ok = 1;
    for (int k = 1; k < 8; ++k) if (fabs(re[k]) > 1e-9 || fabs(im[k]) > 1e-9) others_ok = 0;
    CK(others_ok);

    /* 冲激 -> 平坦谱；逆变换恢复冲激 */
    for (int i = 0; i < 8; ++i) { re[i] = (i == 0); im[i] = 0; }
    fq_fft_1d(&p, re, im, 1, 0);
    int flat_ok = 1;
    for (int k = 0; k < 8; ++k) if (!NEAR(re[k], 1.0, 1e-9) || !NEAR(im[k], 0.0, 1e-9)) flat_ok = 0;
    CK(flat_ok);
    fq_fft_1d(&p, re, im, 1, 1);   /* 无归一化的逆变换：冲激恢复为 8，不是 1 */
    int imp_ok = 1;
    for (int i = 0; i < 8; ++i) if (!NEAR(re[i], (i == 0) ? 8.0 : 0.0, 1e-9)) imp_ok = 0;
    CK(imp_ok);
    fq_plan_free(&p);

    /* 非 2 的幂必须拒绝 */
    FqPlan bad;
    CK(fq_plan_init(&bad, 6) == -1);

    /* 二维往返 */
    FqPlan q;
    CK(fq_plan_init(&q, 8) == 0);
    double r2[64], i2[64];
    for (int i = 0; i < 64; ++i) { r2[i] = (double)((i * 7) % 11); i2[i] = 0.0; }
    double orig[64];
    memcpy(orig, r2, sizeof orig);
    fq_fft_2d(&q, r2, i2, 0);
    fq_fft_2d(&q, r2, i2, 1);
    double err = 0.0;
    for (int i = 0; i < 64; ++i) {
        double d = fabs(r2[i] - orig[i]);
        if (d > err) err = d;
    }
    CK(err < 1e-9);
    fq_plan_free(&q);
}

/* ---------- 无滤波往返 ---------- */
static void test_roundtrip(void) {
    int w = 70, h = 46;              /* 故意非 2 的幂 */
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    synth(buf, w, h, T_CHECKER, 24);
    double err = fq_roundtrip_err(buf, w, h);
    CK(err >= 0.0 && err < 1e-6);
    free(buf);
}

/* ---------- 选截止 + 拒绝 + 纯色 ---------- */
static void test_select(void) {
    unsigned char *img = (unsigned char*)malloc(128u * 128u);
    FqOptions opt;
    fq_options_default(&opt);
    FqReport rep;

    /* 1) 条纹：截止应落在 0.25 cycles/px 附近 */
    rng_seed(1);
    synth(img, 128, 128, T_STRIPES, 24);
    CK(fq_choose_cutoff(img, 128, 128, &opt, &rep) == FQ_OK);
    CK(rep.status == FQ_ST_OK && !rep.flat);
    CK(rep.cutoff > 0.2 && rep.cutoff < 0.31);
    CK(rep.retained > 0.95 && rep.retained <= 1.0);
    CK(rep.sig_bins >= 1);

    /* 2) 棋盘：截止接近 0.354（对角环带） */
    rng_seed(2);
    synth(img, 128, 128, T_CHECKER, 24);
    CK(fq_choose_cutoff(img, 128, 128, &opt, &rep) == FQ_OK);
    CK(rep.cutoff > 0.32 && rep.cutoff < 0.40);

    /* 3) 纯色：单独报出，不做截止，输出恒等 */
    unsigned char *flat = (unsigned char*)malloc(128u * 128u);
    unsigned char *out = (unsigned char*)malloc(128u * 128u);
    memset(flat, 128, 128u * 128u);
    CK(fq_run(flat, 128, 128, &opt, out, &rep) == FQ_OK);
    CK(rep.status == FQ_ST_FLAT && rep.flat == 1);
    CK(rep.cutoff == 0.0);
    CK(memcmp(flat, out, 128u * 128u) == 0);
    CK(strstr(rep.reason, "纯色") != NULL);

    /* 纯色但被量化成略有起伏：仍算纯色（AC 为零） */
    memset(flat, 0, 128u * 128u);
    CK(fq_choose_cutoff(flat, 128, 128, &opt, &rep) == FQ_OK);
    CK(rep.status == FQ_ST_FLAT);

    /* 4) 白噪声：没有显著环带 -> 截止为零，拒绝 */
    rng_seed(3);
    for (size_t i = 0; i < 128u * 128u; ++i) flat[i] = (unsigned char)((rng_u32() >> 16) & 0xffu);
    int rc = fq_choose_cutoff(flat, 128, 128, &opt, &rep);
    CK(rc == FQ_E_CUTOFF_ZERO);
    CK(rep.status == FQ_ST_REJ_ZERO && rep.cutoff == 0.0);
    CK(strstr(rep.reason, "截止频率为零") != NULL);

    /* 5) 角点交替（最高频）：截止超上限，拒绝 */
    rng_seed(4);
    synth(img, 128, 128, T_CORNER, 16);
    rc = fq_choose_cutoff(img, 128, 128, &opt, &rep);
    CK(rc == FQ_E_CUTOFF_HIGH);
    CK(rep.status == FQ_ST_REJ_HIGH);
    CK(rep.cutoff > FQ_CUTOFF_MAX);
    CK(strstr(rep.reason, "超过上限") != NULL);

    /* 6) 上限可调：把上限压到 0.30，条纹也被拒 */
    FqOptions tight = opt;
    tight.cutoff_max = 0.20;
    rng_seed(5);
    synth(img, 128, 128, T_STRIPES, 24);
    CK(fq_choose_cutoff(img, 128, 128, &tight, &rep) == FQ_E_CUTOFF_HIGH);
    CK(rep.cutoff > 0.20);

    /* 7) 参数检查 */
    CK(fq_choose_cutoff(NULL, 128, 128, &opt, &rep) == FQ_E_ARG);
    CK(fq_choose_cutoff(img, 1, 128, &opt, &rep) == FQ_E_DIM);
    CK(fq_run(NULL, 128, 128, &opt, out, &rep) == FQ_E_ARG);

    free(img); free(flat); free(out);
}

/* ---------- 反变换验证：预测保留 ≈ 实测保留 ---------- */
static void test_retention(void) {
    int w = 128, h = 128;
    unsigned char *in = (unsigned char*)malloc(128u * 128u);
    unsigned char *out = (unsigned char*)malloc(128u * 128u);
    FqOptions opt;
    fq_options_default(&opt);
    FqReport rep;
    rng_seed(10);
    synth(in, w, h, T_CHECKER, 24);
    CK(fq_run(in, w, h, &opt, out, &rep) == FQ_OK);
    /* 频域预测的 AC 保留与逆变换后实测的 AC 保留应一致 */
    CK(fabs(rep.retained - rep.retained_meas) < 0.02);
    CK(rep.retained > 0.95);
    CK(rep.roundtrip_err >= 0.0 && rep.roundtrip_err < 1e-6);
    /* 输出不是恒等 */
    CK(memcmp(in, out, 128u * 128u) != 0);
    free(in); free(out);
}

/* ---------- 空域兼容路径：逐字节锁定 ---------- */
static void test_spatial_compat(void) {
    int w = 64, h = 48;
    unsigned char *in = (unsigned char*)malloc(64u * 48u);
    unsigned char *out = (unsigned char*)malloc(64u * 48u);
    rng_seed(20);
    synth(in, w, h, T_CHECKER, 30);

    /* sigma <= 0：逐字节空操作 */
    CK(fq_filter_spatial(in, w, h, 0.0, out) == FQ_OK);
    CK(memcmp(in, out, 64u * 48u) == 0);
    CK(fq_filter_spatial(in, w, h, -1.0, out) == FQ_OK);
    CK(memcmp(in, out, 64u * 48u) == 0);

    /* 同一 sigma 两次调用必须逐字节一致（确定性） */
    unsigned char *a = (unsigned char*)malloc(64u * 48u);
    unsigned char *b = (unsigned char*)malloc(64u * 48u);
    CK(fq_filter_spatial(in, w, h, 1.0, a) == FQ_OK);
    CK(fq_filter_spatial(in, w, h, 1.0, b) == FQ_OK);
    CK(memcmp(a, b, 64u * 48u) == 0);

    /* sigma 很小时等于近似恒等（但不必逐字节） */
    CK(fq_filter_spatial(in, w, h, 0.2, out) == FQ_OK);
    /* 不同 sigma 输出不同 */
    CK(memcmp(a, out, 64u * 48u) != 0);

    free(in); free(out); free(a); free(b);
}

/* ---------- 关掉新逻辑：use_freq=0 与旧空域逐字节一致 ---------- */
static void test_disable_switch(void) {
    int w = 64, h = 48;
    unsigned char *in = (unsigned char*)malloc(64u * 48u);
    unsigned char *out = (unsigned char*)malloc(64u * 48u);
    unsigned char *ref = (unsigned char*)malloc(64u * 48u);
    rng_seed(21);
    synth(in, w, h, T_DIAG, 20);

    FqOptions opt;
    fq_options_default(&opt);
    opt.use_freq = 0;
    opt.sigma = 1.0;
    FqReport rep;
    CK(fq_run(in, w, h, &opt, out, &rep) == FQ_OK);
    CK(fq_filter_spatial(in, w, h, 1.0, ref) == FQ_OK);
    CK(memcmp(out, ref, 64u * 48u) == 0);
    /* 关闭时报告里不应出现频域字段 */
    CK(rep.nfft == 0 && rep.cutoff == 0.0 && rep.roundtrip_err == 0.0);

    /* 频域开启时输出必须不同（说明确实走了新逻辑） */
    opt.use_freq = 1;
    FqOptions def;
    fq_options_default(&def);
    CK(fq_run(in, w, h, &def, out, &rep) == FQ_OK);
    CK(memcmp(out, ref, 64u * 48u) != 0);

    free(in); free(out); free(ref);
}

/* ---------- 模糊度必须比空域低两成 ---------- */
static void test_blur_beats_spatial(void) {
    /* 对四类纹理：
     *   - 频域方案：按能量分布定截止后滤波；
     *   - 空域方案：高斯核，sigma 从 0.2 扫到 3.0。
     * 公平性：只与“高频去除量不低于频域方案”的空域设置比较；
     * 若高斯核再大也追不上频域的去噪量，就取它自己的最大去除量设置。
     * 模糊度以真值纹理为参考，两侧用同一把尺子 fq_blur_degree()。 */
    const int w = 128, h = 128;
    const int kinds[4] = {T_STRIPES, T_CHECKER, T_DIAG, T_DOTS};
    const char *names[4] = {"stripes", "checker", "diag", "dots"};
    unsigned char *in = (unsigned char*)malloc(128u * 128u);
    unsigned char *clean = (unsigned char*)malloc(128u * 128u);
    unsigned char *out = (unsigned char*)malloc(128u * 128u);
    unsigned char *sp = (unsigned char*)malloc(128u * 128u);
    FqOptions opt;
    fq_options_default(&opt);
    FqReport rep;

    for (int k = 0; k < 4; ++k) {
        synth(clean, w, h, kinds[k], 0);
        rng_seed(900 + k);
        for (size_t i = 0; i < (size_t)w * h; ++i) {
            int n = (int)((rng_u32() >> 16) % 256u);
            n = (n * 24) / 256 - 12;
            int v = clean[i] + n;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            in[i] = (unsigned char)v;
        }

        CK(fq_run(in, w, h, &opt, out, &rep) == FQ_OK);
        CK(rep.status == FQ_ST_OK);
        CK(rep.retained > 0.95);
        CK(rep.roundtrip_err < 1e-6);
        CK(fabs(rep.retained - rep.retained_meas) < 0.02);

        double b_freq = fq_blur_degree(out, clean, w, h);
        double hf_freq = fq_hf_removed(out, in, w, h, rep.cutoff);

        double fair_blur = 2.0;
        double max_hf = -1.0, max_hf_blur = 2.0;
        for (double sig = 0.2; sig <= 3.01; sig += 0.1) {
            CK(fq_filter_spatial(in, w, h, sig, sp) == FQ_OK);
            double bs = fq_blur_degree(sp, clean, w, h);
            double hs = fq_hf_removed(sp, in, w, h, rep.cutoff);
            if (hs > max_hf) { max_hf = hs; max_hf_blur = bs; }
            if (hs >= hf_freq - 0.01 && bs < fair_blur) fair_blur = bs;
        }
        if (fair_blur > 1.5) fair_blur = max_hf_blur;   /* 追不上就去噪最强的一档 */

        double imp = fair_blur > 1e-9 ? (1.0 - b_freq / fair_blur) * 100.0 : 0.0;
        printf("  %-8s fc=%.4f retained=%.4f blur_freq=%.4f blur_spatial_fair=%.4f "
               "hf_freq=%.4f hf_spatial_max=%.4f improvement=%.1f%%\n",
               names[k], rep.cutoff, rep.retained, b_freq, fair_blur,
               hf_freq, max_hf, imp);

        /* 频域方案必须真的去掉了大量高频，比较才有意义 */
        CK(hf_freq > 0.5);
        /* 硬性要求：模糊度至少比同等去噪的空域方案低两成 */
        CK(b_freq <= 0.8 * fair_blur + 1e-9);
    }
    free(in); free(clean); free(out); free(sp);
}

/* ---------- 边界延拓与尺寸 ---------- */
static void test_shapes(void) {
    const int dims[][2] = {{128,128},{96,80},{80,96},{70,70},{200,150},{33,47}};
    for (unsigned i = 0; i < sizeof dims / sizeof dims[0]; ++i) {
        int w = dims[i][0], h = dims[i][1];
        unsigned char *in = (unsigned char*)malloc((size_t)w * h);
        unsigned char *out = (unsigned char*)malloc((size_t)w * h);
        synth(in, w, h, T_CHECKER, 16);
        FqOptions opt;
        fq_options_default(&opt);
        FqReport rep;
        int rc = fq_run(in, w, h, &opt, out, &rep);
        CK(rc == FQ_OK);
        CK(rep.status == FQ_ST_OK);
        CK(rep.retained_meas > 0.8);
        double err = fq_roundtrip_err(in, w, h);
        CK(err >= 0.0 && err < 1e-6);
        free(in); free(out);
    }
}

int main(void) {
    printf("freq module tests\n");
    test_fft();
    test_roundtrip();
    test_select();
    test_retention();
    test_spatial_compat();
    test_disable_switch();
    test_blur_beats_spatial();
    test_shapes();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
