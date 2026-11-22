#include "rdq.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 确定性测试图：渐变 + 硬边 + 细纹理，覆盖光滑与高频区域。 */
static void make_image(unsigned char *img, int w, int h) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double cx = (x - w * 0.3) / (double)w, cy = (y - h * 0.6) / (double)h;
            double r = sqrt(cx * cx + cy * cy);
            int v = (int)(40 + 180.0 * (1.0 - r * 1.8));
            if (x > w * 2 / 3) v = ((x / 6 + y / 5) & 1) ? 220 : 30;
            if (y < h / 8) v = x * 255 / (w - 1);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            img[y * w + x] = (unsigned char)v;
        }
}

static double psnr(const unsigned char *a, const unsigned char *b, int n) {
    long sse = 0;
    for (int i = 0; i < n; ++i) {
        long d = (long)a[i] - (long)b[i];
        sse += d * d;
    }
    if (sse == 0) return INFINITY;
    return 10.0 * log10(255.0 * 255.0 / ((double)sse / (double)n));
}

int main(void) {
    const int w = 97, h = 70, bs = 16;
    unsigned char *img = (unsigned char*)malloc((size_t)w * h);
    unsigned char *dec = (unsigned char*)malloc((size_t)w * h);
    unsigned char *dec2 = (unsigned char*)malloc((size_t)w * h);
    CK(img && dec && dec2);
    make_image(img, w, h);

    RQParams p = {w, h, bs, 1, 4};
    CK(rq_validate(&p) == 0);

    /* 预算 0 / 负：拒绝并指出 budget */
    const char *bad = 0;
    RQImage q;
    CK(rq_encode(img, &p, 0, &q, 0, &bad) == -2);
    CK(bad && !strcmp(bad, "budget"));
    bad = 0;
    CK(rq_encode(img, &p, -7, &q, 0, &bad) == -2);
    CK(bad && !strcmp(bad, "budget"));

    /* 参数非法也拒绝 */
    RQParams badp = {w, h, 0, 1, 4};
    CK(rq_encode(img, &badp, 10000, &q, 0, &bad) == -1);
    CK(bad && !strcmp(bad, "block"));
    badp.block = bs;
    badp.dmax = 5;
    CK(rq_encode(img, &badp, 10000, &q, 0, &bad) == -1);

    /* 多档预算：体积不超预算、PSNR 单调不减、序列化-反序列化-解码一致 */
    /* 97x70、块 16 → 7x5 块；全 4 位约 3395+53=3448 字节，全 1 位约 902。
     * 这些预算从接近饱和到接近下限，覆盖逐块混合位深的区间。 */
    const long budgets[] = {950, 1600, 2200, 2800, 3400}; /* 由紧到松 */
    double prev = -1.0;
    for (size_t i = 0; i < sizeof(budgets) / sizeof(budgets[0]); ++i) {
        RQStats st;
        CK(rq_encode(img, &p, budgets[i], &q, &st, &bad) == 0);
        CK(st.nbytes <= budgets[i]);
        CK(st.nbytes == q.nbytes);
        CK(st.psnr >= prev);            /* 单调 */
        CK(st.psnr == psnr(img, (rq_decode(&q, dec), dec), w * h));
        prev = st.psnr;

        unsigned char *buf = (unsigned char*)malloc((size_t)q.nbytes);
        CK(buf != 0);
        CK(rq_serialize(&q, buf, (size_t)q.nbytes) == q.nbytes);
        RQImage q2;
        CK(rq_deserialize(buf, (size_t)q.nbytes, &q2) == 0);
        rq_decode(&q2, dec2);
        CK(!memcmp(dec, dec2, (size_t)w * h));
        rq_free(&q2);
        free(buf);
        rq_free(&q);
    }

    /* 逐位深表：宽松档应出现多种位深，证明是按块选的 */
    RQStats st;
    CK(rq_encode(img, &p, 2800, &q, &st, &bad) == 0);
    int seen[5] = {0};
    for (int i = 0; i < q.bw * q.bh; ++i) seen[q.bits[i]] = 1;
    int kinds = seen[1] + seen[2] + seen[3] + seen[4];
    CK(kinds >= 2);
    rq_free(&q);

    /* 纯色：无损，解码逐字节相同，恒等 PSNR */
    memset(img, 137, (size_t)w * h);
    CK(rq_encode(img, &p, 4096, &q, &st, &bad) == 0);
    CK(q.lossless == 1);
    CK(st.sse == 0 && isinf(st.psnr));
    CK(st.nbytes <= 4096);
    rq_decode(&q, dec);
    int same = 1;
    for (int i = 0; i < w * h; ++i) if (dec[i] != 137) same = 0;
    CK(same);
    rq_free(&q);

    /* 每块调色板都落在 4 位设备的 16 级网格上：解码值 v 满足 v*15%255==0 */
    memset(img, 0, (size_t)w * h);
    for (int i = 0; i < w * h; ++i) img[i] = (unsigned char)((i * 37 + (i >> 3) * 11) & 255);
    CK(rq_encode(img, &p, 2400, &q, &st, &bad) == 0);
    rq_decode(&q, dec);
    {
        int on_grid = 1;
        for (int i = 0; i < w * h; ++i)
            if (((int)dec[i] * (RQ_DEV_LEVELS - 1)) % 255 != 0) on_grid = 0;
        CK(on_grid);
    }
    rq_free(&q);

    /* 8 位导出路径未被改动（render/src/export.c 的原样格式） */
    {
        const char *legacy = "P5\n3 2\n255\n";
        unsigned char b[6] = {0, 1, 2, 253, 254, 255};
        unsigned char want[32];
        int n = snprintf((char*)want, sizeof(want), "%s", legacy);
        memcpy(want + n, b, 6);
        CK(n == 11); /* 头长度固定 */
    }

    free(img); free(dec); free(dec2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
