#define _POSIX_C_SOURCE 199309L
#include "blend.h"
#include <math.h>
#include <string.h>
#include <time.h>

#define EPS_DET 1e-12

const char *blend_name(BlendMode m) {
    switch (m) {
        case BLEND_NORMAL:     return "normal";
        case BLEND_MULTIPLY:   return "multiply";
        case BLEND_SCREEN:     return "screen";
        case BLEND_PLUS:       return "plus";
        case BLEND_DIFFERENCE: return "difference";
        case BLEND_EXCLUSION:  return "exclusion";
        case BLEND_DARKEN:     return "darken";
        case BLEND_DODGE:      return "color-dodge";
        default:               return "?";
    }
}

static int clamp01(double x) { return x >= 0.0 && x <= 1.0; }
static double clampc(double x) { return x < 0.0 ? 0.0 : (x > 1.0 ? 1.0 : x); }

/* 单通道仿射系数：c' = a*c + b。
 * 返回 0 成功；-11 表示该模式不是仿射的。 */
static int channel_affine(BlendMode mode, double s, double *a, double *b) {
    switch (mode) {
        case BLEND_NORMAL:     *a = 0.0;          *b = s;            return 0;
        case BLEND_MULTIPLY:   *a = s;            *b = 0.0;          return 0;
        case BLEND_SCREEN:     *a = 1.0 - s;      *b = s;            return 0;
        case BLEND_PLUS:       *a = 1.0;          *b = s;            return 0;
        case BLEND_EXCLUSION:  *a = 1.0 - 2.0*s;  *b = s;            return 0;
        case BLEND_DIFFERENCE:
        case BLEND_DARKEN:
        case BLEND_DODGE:
        default:              return -11;
    }
}

/* 非仿射模式的原始 W3C 公式（截断到 0..1）。 */
static double channel_raw(BlendMode mode, double c, double s) {
    switch (mode) {
        case BLEND_DIFFERENCE: return fabs(c - s);
        case BLEND_DARKEN:     return fmin(c, s);
        case BLEND_DODGE:
            if (s >= 1.0) return 1.0;
            if (c <= 0.0) return 0.0;
            return clampc(c / (1.0 - s));
        default: return c;
    }
}

int blend_matrix(BlendMode mode, double sr, double sg, double sb, BMat *out) {
    if (!out || !clamp01(sr) || !clamp01(sg) || !clamp01(sb) || mode < 0 || mode >= BLEND_N)
        return -1;
    double ss[3] = {sr, sg, sb};
    memset(out, 0, sizeof(*out));
    for (int ch = 0; ch < 3; ++ch) {
        double a, b;
        int rc = channel_affine(mode, ss[ch], &a, &b);
        if (rc != 0) return rc;
        out->m[ch][ch] = a;
        out->m[ch][3]  = b;
    }
    out->m[3][3] = 1.0;
    return 0;
}

BMat bmat_mul(const BMat *a, const BMat *b) {
    BMat r;
    memset(&r, 0, sizeof(r));
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) {
            double aik = a->m[i][k];
            for (int j = 0; j < 4; ++j)
                r.m[i][j] += aik * b->m[k][j];
        }
    return r;
}

void bmat_apply(const BMat *a, const double in[3], double out[3]) {
    double v[4] = {in[0], in[1], in[2], 1.0};
    for (int i = 0; i < 3; ++i) {
        double s = 0.0;
        for (int j = 0; j < 4; ++j) s += a->m[i][j] * v[j];
        out[i] = s;
    }
}

int blend_forward(const BlendLayer *layers, const int *order, int n,
                  const double base[3], double out[3]) {
    if (!layers || !order || !base || !out || n < 1 || n > BLEND_MAX_LAYERS) return -1;
    double cur[3];
    memcpy(cur, base, sizeof(cur));
    for (int k = 0; k < n; ++k) {
        const BlendLayer *L = &layers[order[k]];
        if (L->mode < 0 || L->mode >= BLEND_N) return -1;
        if (!clamp01(L->r) || !clamp01(L->g) || !clamp01(L->b)) return -1;
        double ss[3] = {L->r, L->g, L->b};
        BMat M;
        int rc = blend_matrix(L->mode, ss[0], ss[1], ss[2], &M);
        if (rc == 0) {
            double nx[3];
            bmat_apply(&M, cur, nx);
            for (int ch = 0; ch < 3; ++ch) cur[ch] = clampc(nx[ch]);
        } else if (rc == -11) {
            for (int ch = 0; ch < 3; ++ch)
                cur[ch] = clampc(channel_raw(L->mode, cur[ch], ss[ch]));
        } else {
            return rc;
        }
    }
    memcpy(out, cur, sizeof(cur));
    return 0;
}

int blend_invertible(const BlendLayer *layer, int *pivot) {
    if (pivot) *pivot = -1;
    if (!layer || layer->mode < 0 || layer->mode >= BLEND_N) return -1;
    double ss[3] = {layer->r, layer->g, layer->b};
    for (int ch = 0; ch < 3; ++ch) {
        double a, b;
        int rc = channel_affine(layer->mode, ss[ch], &a, &b);
        if (rc != 0) return -11;
        if (fabs(a) <= EPS_DET) { if (pivot) *pivot = ch; return -10; }
    }
    return 0;
}

/* ---------- sRGB -> Lab，CIE76 Delta E ---------- */

static double srgb_to_linear(double c) {
    c = clampc(c);
    return c <= 0.04045 ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

static void rgb_to_lab(const double rgb[3], double lab[3]) {
    double r = srgb_to_linear(rgb[0]);
    double g = srgb_to_linear(rgb[1]);
    double b = srgb_to_linear(rgb[2]);
    /* sRGB D65 -> XYZ */
    double x = (r * 0.4124564 + g * 0.3575761 + b * 0.1804375) / 0.95047;
    double y = (r * 0.2126729 + g * 0.7151522 + b * 0.0721750) / 1.00000;
    double z = (r * 0.0193339 + g * 0.1191920 + b * 0.9503041) / 1.08883;
    double f(double t) {
        return t > 0.008856 ? cbrt(t) : (7.787 * t + 16.0 / 116.0);
    }
    double fx = f(x), fy = f(y), fz = f(z);
    lab[0] = 116.0 * fy - 16.0;
    lab[1] = 500.0 * (fx - fy);
    lab[2] = 200.0 * (fy - fz);
}

double blend_delta_e(const double c1[3], const double c2[3]) {
    double l1[3], l2[3];
    rgb_to_lab(c1, l1);
    rgb_to_lab(c2, l2);
    double s = 0.0;
    for (int i = 0; i < 3; ++i) {
        double d = l1[i] - l2[i];
        s += d * d;
    }
    return sqrt(s);
}

/* ---------- 顺序反解 ---------- */

static long nanos_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000000000L + ts.tv_nsec;
}

/* 堆算法枚举全排列，逐排列前向合成并比对色差。 */

int blend_solve(const BlendLayer *layers, int n,
                const double base[3], const double target[3], double tol,
                BlendSolution *sol, int *single, int *bad_step, int *bad_code) {
    if (single) *single = 0;
    if (bad_step) *bad_step = -1;
    if (bad_code) *bad_code = 0;
    if (!layers || !base || !target || !sol || n < 1 || n > BLEND_MAX_LAYERS || tol < 0.0)
        return -1;

    long t0 = nanos_now();

    /* 单层：直接输出，不搜索。 */
    if (n == 1) {
        sol->order[0] = 0;
        int rc = blend_forward(layers, sol->order, 1, base, sol->achieved);
        if (rc != 0) {
            if (bad_step) *bad_step = 0;
            if (bad_code) *bad_code = rc;
            return rc;
        }
        sol->delta = blend_delta_e(sol->achieved, target);
        sol->tried = 1;
        sol->elapsed_ns = nanos_now() - t0;
        if (single) *single = 1;
        return sol->delta <= tol ? 0 : -12;
    }

    /* 先逐层做可逆性检查，不可反解立即拒绝并指出步骤。 */
    for (int i = 0; i < n; ++i) {
        int pivot = -1;
        int rc = blend_invertible(&layers[i], &pivot);
        if (rc != 0) {
            if (bad_step) *bad_step = i;
            if (bad_code) *bad_code = rc;
            sol->elapsed_ns = nanos_now() - t0;
            return rc;
        }
    }

    int perm[BLEND_MAX_LAYERS];
    for (int i = 0; i < n; ++i) perm[i] = i;
    int c[BLEND_MAX_LAYERS] = {0};

    sol->delta = 1e300;
    sol->tried = 0;
    int best[BLEND_MAX_LAYERS];
    double best_rgb[3];

    /* 标准迭代 Heap 全排列：枚举完 n! 个排列，保证得到全局最优顺序。 */
    for (;;) {
        double got[3];
        int rc = blend_forward(layers, perm, n, base, got);
        if (rc != 0) {
            if (bad_step) *bad_step = -1;
            if (bad_code) *bad_code = rc;
            return rc;
        }
        double d = blend_delta_e(got, target);
        ++sol->tried;
        if (d < sol->delta) {
            sol->delta = d;
            memcpy(best, perm, sizeof(int) * (size_t)n);
            memcpy(best_rgb, got, sizeof(best_rgb));
        }

        int i = 0;
        while (i < n && c[i] >= i) { c[i] = 0; ++i; }
        if (i >= n) break;
        int sw = (i % 2 == 0) ? 0 : c[i];
        int tmp = perm[sw]; perm[sw] = perm[i]; perm[i] = tmp;
        ++c[i];
    }

    memcpy(sol->order, best, sizeof(int) * (size_t)n);
    memcpy(sol->achieved, best_rgb, sizeof(best_rgb));
    sol->elapsed_ns = nanos_now() - t0;
    if (sol->delta > tol) return -12;
    return 0;
}
