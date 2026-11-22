#define _POSIX_C_SOURCE 199309L
#include "rdq.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 四档预算（宽松→极紧）的率失真演示。
 * 每档输出：预算、实际体积、占预算比、PSNR、编码耗时。
 * 同时演示纯色无损、非法预算拒绝，并对单调性与 1% 预算红线做断言。 */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static unsigned char *make_gradient(int w, int h) {
    unsigned char *img = (unsigned char*)malloc((size_t)w * h);
    if (!img) return 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double u = w > 1 ? x / (double)(w - 1) : 0.0;
            const double v = h > 1 ? y / (double)(h - 1) : 0.0;
            double r = sqrt((u - 0.35) * (u - 0.35) + (v - 0.55) * (v - 0.55));
            int g = (int)(210.0 - 150.0 * r);
            if (x > w * 2 / 3) { /* 高频棋盘 */
                g = ((x / 5 + y / 4) & 1) ? 235 : 20;
            }
            if (y < h / 7) g = w > 1 ? x * 255 / (w - 1) : 0; /* 平滑渐变条 */
            if (g < 0) g = 0;
            if (g > 255) g = 255;
            img[y * w + x] = (unsigned char)g;
        }
    return img;
}

static int run_case(const char *name, const unsigned char *img,
                    const RQParams *p, long budget, double *out_psnr) {
    RQStats st;
    const char *bad = 0;
    const double t0 = now_ms();
    RQImage q;
    const int rc = rq_encode(img, p, budget, &q, &st, &bad);
    const double dt = now_ms() - t0;
    if (rc != 0) {
        printf("%-10s budget %7ld  REJECT rc=%d param=%s\n", name, budget, rc, bad ? bad : "?");
        return -1;
    }
    const double ratio = budget > 0 ? 100.0 * (double)st.nbytes / (double)budget : 0.0;
    printf("%-10s budget %7ld  bytes %7d (%5.1f%%)  PSNR %7.3f dB  %7.2f ms  bits",
           name, budget, st.nbytes, ratio, st.psnr, dt);
    for (int d = q.dmin; d <= q.dmax; ++d) {
        int n = 0;
        if (!q.lossless)
            for (int i = 0; i < q.bw * q.bh; ++i) if (q.bits[i] == d) ++n;
        printf(" %d:%d", d, n);
    }
    printf("\n");
    int ok = 1;
    if ((double)st.nbytes > (double)budget * 1.01) {
        printf("  FAIL 体积超出预算 1%%\n");
        ok = 0;
    }
    if (q.nbytes != st.nbytes) { printf("  FAIL nbytes 不一致\n"); ok = 0; }
    if (out_psnr) *out_psnr = st.psnr;
    rq_free(&q);
    return ok ? 0 : 1;
}

int main(void) {
    const int w = 128, h = 96;
    unsigned char *img = make_gradient(w, h);
    if (!img) return 1;

    RQParams p = {w, h, 16, 1, RQ_DEV_BITS};
    const long npix = (long)w * h;
    /* 预算档位：宽松到极紧。上限取全 4 位理论体积的 ~90%，往下留出
     * 三档可分辨的紧度；再往上加预算不会再有收益（已全 4 位）。 */
    const long b_loose = npix * RQ_DEV_BITS / 8 * 9 / 10 + 32;
    const long b_mid1 = (long)(b_loose * 0.78);
    const long b_mid2 = (long)(b_loose * 0.58);
    const long b_tight = (long)(b_loose * 0.40);
    const long budgets[4] = {b_loose, b_mid1, b_mid2, b_tight};

    printf("== 四档预算（%dx%d，块 %d，候选位深 %d..%d）==\n",
           w, h, p.block, p.dmin, p.dmax);
    int fails = 0;
    double prev = -1.0;
    for (int i = 0; i < 4; ++i) {
        double ps = 0.0;
        const int rc = run_case("gradient", img, &p, budgets[i], &ps);
        if (rc) { ++fails; if (rc < 0) continue; }
        if (i > 0 && ps > prev + 1e-9) {
            printf("  FAIL PSNR 非单调：预算降低但 PSNR 升高 %.3f > %.3f\n", ps, prev);
            ++fails;
        }
        prev = ps;
    }

    /* 纯色：无损 */
    printf("== 纯色 ==\n");
    unsigned char solid[64 * 48];
    memset(solid, 128, sizeof(solid));
    RQParams ps_ = {64, 48, 16, 1, RQ_DEV_BITS};
    RQStats st;
    const char *bad = 0;
    RQImage qs;
    if (rq_encode(solid, &ps_, 4096, &qs, &st, &bad) != 0 || !qs.lossless) {
        printf("  FAIL 纯色未走无损\n");
        ++fails;
    } else {
        unsigned char *dec = (unsigned char*)malloc(sizeof(solid));
        rq_decode(&qs, dec);
        int same = !memcmp(dec, solid, sizeof(solid));
        printf("  lossless=%d bytes=%d 逐字节还原=%s\n",
               qs.lossless, st.nbytes, same ? "是" : "否");
        if (!same) ++fails;
        free(dec);
        rq_free(&qs);
    }

    /* 非法预算：0 / 负，必须拒绝并指出参数 */
    printf("== 非法预算 ==\n");
    for (int t = 0; t < 2; ++t) {
        const long b = t == 0 ? 0 : -100;
        RQImage q;
        bad = 0;
        const int rc = rq_encode(img, &p, b, &q, 0, &bad);
        printf("  budget %5ld -> rc=%d param=%s\n", b, rc, bad ? bad : "(null)");
        if (rc >= 0 || !bad || strcmp(bad, "budget") != 0) ++fails;
    }

    free(img);
    printf(fails ? "FAIL %d\n" : "PASS\n", fails);
    return fails ? 1 : 0;
}
