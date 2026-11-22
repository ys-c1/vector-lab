#include "fft.h"
#include <math.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* bit-reversal 置换表：rev[i] = i 的位反转 */
static int build_rev(int n, int *rev) {
    int bits = 0;
    while ((1 << bits) < n) ++bits;
    for (int i = 0; i < n; ++i) {
        int r = 0;
        for (int b = 0; b < bits; ++b) if (i & (1 << b)) r |= 1 << (bits - 1 - b);
        rev[i] = r;
    }
    return 0;
}

int fq_plan_init(FqPlan *p, int n) {
    if (!p || n < 2 || (n & (n - 1)) != 0) return -1;
    p->n = n;
    p->rev = (int*)malloc(sizeof(int) * (size_t)n);
    p->cos_t = (double*)malloc(sizeof(double) * (size_t)(n / 2 > 0 ? n / 2 : 1));
    p->sin_t = (double*)malloc(sizeof(double) * (size_t)(n / 2 > 0 ? n / 2 : 1));
    if (!p->rev || !p->cos_t || !p->sin_t) {
        fq_plan_free(p);
        return -2;
    }
    build_rev(n, p->rev);
    for (int k = 0; k < n / 2; ++k) {
        double a = 2.0 * M_PI * (double)k / (double)n;
        p->cos_t[k] = cos(a);
        p->sin_t[k] = sin(a);
    }
    return 0;
}

void fq_plan_free(FqPlan *p) {
    if (!p) return;
    free(p->rev); free(p->cos_t); free(p->sin_t);
    p->rev = NULL; p->cos_t = NULL; p->sin_t = NULL;
    p->n = 0;
}

/* 原地一维 FFT（stride 允许对列做连续访问）。
 * inverse 时不做 1/n 归一化，由调用方统一缩放。 */
void fq_fft_1d(const FqPlan *p, double *re, double *im, int stride, int inverse) {
    int n = p->n;
    for (int i = 0; i < n; ++i) {
        int j = p->rev[i];
        if (j > i) {
            double tr = re[(size_t)i * stride], ti = im[(size_t)i * stride];
            re[(size_t)i * stride] = re[(size_t)j * stride];
            im[(size_t)i * stride] = im[(size_t)j * stride];
            re[(size_t)j * stride] = tr;
            im[(size_t)j * stride] = ti;
        }
    }
    for (int len = 2; len <= n; len <<= 1) {
        int half = len >> 1;
        int tab = n / len;
        for (int i = 0; i < n; i += len) {
            for (int k = 0; k < half; ++k) {
                double wr = p->cos_t[k * tab];
                double wi = inverse ? p->sin_t[k * tab] : -p->sin_t[k * tab];
                int a = i + k, b = i + k + half;
                double xr = re[(size_t)b * stride], xi = im[(size_t)b * stride];
                double tr = xr * wr - xi * wi;
                double ti = xr * wi + xi * wr;
                double ur = re[(size_t)a * stride], ui = im[(size_t)a * stride];
                re[(size_t)a * stride] = ur + tr;
                im[(size_t)a * stride] = ui + ti;
                re[(size_t)b * stride] = ur - tr;
                im[(size_t)b * stride] = ui - ti;
            }
        }
    }
}

void fq_fft_2d(const FqPlan *p, double *re, double *im, int inverse) {
    int n = p->n;
    /* 行 */
    for (int y = 0; y < n; ++y)
        fq_fft_1d(p, re + (size_t)y * n, im + (size_t)y * n, 1, inverse);
    /* 列 */
    for (int x = 0; x < n; ++x)
        fq_fft_1d(p, re + x, im + x, n, inverse);
    if (inverse) {
        double s = 1.0 / (double)(n * n);
        size_t total = (size_t)n * (size_t)n;
        for (size_t i = 0; i < total; ++i) { re[i] *= s; im[i] *= s; }
    }
}
