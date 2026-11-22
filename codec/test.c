#include "src/wavelet.h"
#include "src/quant.h"
#include "src/sbc.h"
#include "src/pgm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void fill_pattern(uint8_t *px, int w, int h, int kind) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v;
            switch (kind) {
                case 0: v = 137; break;                                     /* 纯色 */
                case 1: v = ((x / 16 + y / 16) & 1) ? 255 : 0; break;       /* 棋盘（二元） */
                case 2: v = (int)(128 + 100 * sin(0.15 * x) * cos(0.11 * y)); break; /* 照片样平滑 */
                default: v = (x * 7 + y * 13 + (x * y) * 3) & 255;         /* 高频纹理 */
            }
            px[y * w + x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

static void test_wavelet(void) {
    int w = 37, h = 31, n = w * h; /* 含奇数边长 */
    int32_t *a = malloc(n * sizeof(int32_t));
    int32_t *ref = malloc(n * sizeof(int32_t));
    for (int i = 0; i < n; ++i) { a[i] = (i * 1103515245u) % 511 - 255; ref[i] = a[i]; }
    WBand bands[25];
    int L = wavelet_levels(w, h);
    CK(L >= 1 && L <= 6);
    int nb = wavelet_forward(a, w, h, L, bands);
    CK(nb == 1 + 3 * L);
    CK(wavelet_inverse(a, w, h, L) == 0);
    int ok = 1;
    for (int i = 0; i < n; ++i) if (a[i] != ref[i]) { ok = 0; break; }
    CK(ok); /* 整数提升必须完全可逆 */
    WBand lay[25];
    CK(wavelet_layout(w, h, L, lay) == nb);
    for (int b = 0; b < nb; ++b)
        CK(lay[b].x == bands[b].x && lay[b].y == bands[b].y &&
           lay[b].w == bands[b].w && lay[b].h == bands[b].h);
    free(a); free(ref);
}

static void test_step_inversion(void) {
    int w = 64, h = 64, n = w * h;
    uint8_t *px = malloc(n);
    fill_pattern(px, w, h, 2);
    int32_t *coef = malloc(n * sizeof(int32_t));
    for (int i = 0; i < n; ++i) coef[i] = px[i];
    WBand bands[25];
    int L = wavelet_levels(w, h);
    int nb = wavelet_forward(coef, w, h, L, bands);
    QBandStat st[25];
    CK(quant_analyze(coef, w, bands, nb, quant_preset_base(Q_BALANCED), st) == 0);
    CK(quant_check_steps(st, nb) == 0);
    /* 所有步长严格为正 */
    for (int b = 0; b < nb; ++b) CK(st[b].step_q16 > 0);
    /* 找能量最高与最低的非常数带，验证“能量高步长小”的单调关系 */
    int hi = -1, lo = -1;
    for (int b = 0; b < nb; ++b) {
        if (st[b].constant) continue;
        if (hi < 0 || st[b].energy > st[hi].energy) hi = b;
        if (lo < 0 || st[b].energy < st[lo].energy) lo = b;
    }
    CK(hi >= 0 && lo >= 0 && hi != lo);
    CK(st[hi].energy > st[lo].energy && st[hi].step_q16 < st[lo].step_q16);
    /* base 为零必须拒绝 */
    CK(quant_analyze(coef, w, bands, nb, 0, st) == -3);
    /* 步长为零必须拒绝（负值在无符号类型上不可表达，解码侧对越界值拒绝） */
    QBandStat bad[1] = {{0.0, 0, 1, 0u}};
    CK(quant_check_steps(bad, 1) == -2);
    bad[0].step_q16 = 65536u;
    CK(quant_check_steps(bad, 1) == 0);
    free(px); free(coef);
}

static void test_solid_lossless(void) {
    int w = 48, h = 40, n = w * h;
    uint8_t *px = malloc(n), *dec = malloc(n);
    size_t cap = sbc_max_size(w, h);
    uint8_t *buf = malloc(cap);
    for (int kind = 0; kind <= 0; ++kind) {
        fill_pattern(px, w, h, kind);
        for (int q = 0; q < 4; ++q) {
            int len = sbc_encode(px, w, h, (QPreset)q, buf, cap);
            CK(len > 0);
            SbcInfo info;
            CK(sbc_decode(buf, (size_t)len, dec, n, &info) == 0);
            CK(info.lossless == 1);
            CK(memcmp(px, dec, n) == 0); /* 纯色逐像素无损 */
            CK(sbc_psnr(px, dec, n) >= 99.0 - 1e-9);
        }
    }
    free(px); free(dec); free(buf);
}

static void test_roundtrip_and_reject(void) {
    int w = 53, h = 41, n = w * h; /* 奇数尺寸 */
    uint8_t *px = malloc(n), *dec = malloc(n);
    size_t cap = sbc_max_size(w, h);
    uint8_t *buf = malloc(cap);
    for (int kind = 1; kind <= 3; ++kind) {
        fill_pattern(px, w, h, kind);
        for (int q = 0; q < 4; ++q) {
            int len = sbc_encode(px, w, h, (QPreset)q, buf, cap);
            CK(len > 0 && (size_t)len < cap);
            SbcInfo info;
            CK(sbc_decode(buf, (size_t)len, dec, n, &info) == 0);
            CK(sbc_psnr(px, dec, n) >= 28.0); /* 往返质量下限；正式 32dB 门槛由 bench 把关 */
        }
    }
    /* 损坏/非法输入必须拒绝，不得崩溃 */
    int len = sbc_encode(px, w, h, Q_TINY, buf, cap);
    CK(sbc_decode((uint8_t*)"xxxx", 4, dec, n, NULL) < 0);          /* 坏魔数 */
    uint8_t *bad = malloc((size_t)len);
    memcpy(bad, buf, (size_t)len);
    bad[8] = 9;                                                      /* 非法级数 */
    CK(sbc_decode(bad, (size_t)len, dec, n, NULL) < 0);
    memcpy(bad, buf, (size_t)len);
    bad[7] = 0;                                                      /* 高度为零 */
    CK(sbc_decode(bad, (size_t)len, dec, n, NULL) < 0);
    memcpy(bad, buf, (size_t)len);
    bad[len - 1] ^= 0xFFu;                                           /* 负载损坏 */
    CK(sbc_decode(bad, (size_t)len, dec, n, NULL) < 0);
    memcpy(bad, buf, (size_t)len);
    bad[15] = (uint8_t)(bad[15] + 1);                                /* 声明长度不符 */
    CK(sbc_decode(bad, (size_t)len, dec, n, NULL) < 0);
    free(bad); free(px); free(dec); free(buf);
}

int main(void) {
    test_wavelet();
    test_step_inversion();
    test_solid_lossless();
    test_roundtrip_and_reject();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
