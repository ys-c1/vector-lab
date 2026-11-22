#include "src/blend.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* ---- 独立参照：直接按 W3C 单通道公式前向，不经过矩阵代码 ---- */
static double ref_channel(BlendMode m, double c, double s) {
    double v;
    switch (m) {
        case BLEND_NORMAL:     v = s; break;
        case BLEND_MULTIPLY:   v = c * s; break;
        case BLEND_SCREEN:     v = c + s - c * s; break;
        case BLEND_PLUS:       v = c + s; break;
        case BLEND_EXCLUSION:  v = c + s - 2.0 * c * s; break;
        case BLEND_DIFFERENCE: v = fabs(c - s); break;
        case BLEND_DARKEN:     v = fmin(c, s); break;
        case BLEND_DODGE:
            if (s >= 1.0) v = 1.0;
            else if (c <= 0.0) v = 0.0;
            else v = c / (1.0 - s);
            break;
        default: v = c; break;
    }
    return fmin(1.0, fmax(0.0, v));
}

static void ref_forward(const BlendLayer *L, const int *order, int n,
                        const double base[3], double out[3]) {
    double cur[3];
    memcpy(cur, base, sizeof(cur));
    for (int k = 0; k < n; ++k) {
        double ss[3] = {L[order[k]].r, L[order[k]].g, L[order[k]].b};
        for (int ch = 0; ch < 3; ++ch)
            cur[ch] = ref_channel(L[order[k]].mode, cur[ch], ss[ch]);
    }
    memcpy(out, cur, sizeof(cur));
}

/* 独立参照求解：全排列暴力搜索，返回最小色差对应顺序。 */
static void ref_solve(const BlendLayer *L, int n, const double base[3],
                      const double target[3], int *best_order, double *best_d,
                      double best_rgb[3]) {
    int perm[BLEND_MAX_LAYERS];
    for (int i = 0; i < n; ++i) perm[i] = i;
    int cc[BLEND_MAX_LAYERS] = {0};
    *best_d = 1e300;
    for (;;) {
        double got[3];
        ref_forward(L, perm, n, base, got);
        double d = blend_delta_e(got, target);
        if (d < *best_d) {
            *best_d = d;
            memcpy(best_order, perm, sizeof(int) * (size_t)n);
            memcpy(best_rgb, got, sizeof(got));
        }
        int i = 0;
        while (i < n && cc[i] >= i) { cc[i] = 0; ++i; }
        if (i >= n) break;
        int sw = (i % 2 == 0) ? 0 : cc[i];
        int tmp = perm[sw]; perm[sw] = perm[i]; perm[i] = tmp;
        ++cc[i];
    }
}

int main(void) {
    /* 1. 矩阵系数：每个仿射模式抽样比对 4x4 作用结果与直接公式。 */
    BlendMode aff_modes[] = {BLEND_NORMAL, BLEND_MULTIPLY, BLEND_SCREEN,
                             BLEND_PLUS, BLEND_EXCLUSION};
    double probes[] = {0.1, 0.37, 0.62, 0.9};
    for (unsigned mi = 0; mi < sizeof(aff_modes) / sizeof(aff_modes[0]); ++mi) {
        BlendLayer L = {aff_modes[mi], 0.25, 0.5, 0.75};
        BMat M;
        CK(blend_matrix(L.mode, L.r, L.g, L.b, &M) == 0);
        for (unsigned p = 0; p < sizeof(probes) / sizeof(probes[0]); ++p) {
            double in[3] = {probes[p], probes[p], probes[p]};
            double got[3], ref[3];
            bmat_apply(&M, in, got);
            double ss[3] = {L.r, L.g, L.b};
            for (int ch = 0; ch < 3; ++ch) ref[ch] = ref_channel(L.mode, in[ch], ss[ch]);
            for (int ch = 0; ch < 3; ++ch) {
                /* 矩阵是截断前的线性仿射；截断后与参照一致。 */
                double gc = fmin(1.0, fmax(0.0, got[ch]));
                CK(fabs(gc - ref[ch]) < 1e-12);
            }
        }
    }

    /* 2. 非仿射模式必须拒绝出矩阵。 */
    BMat M;
    CK(blend_matrix(BLEND_DIFFERENCE, 0.5, 0.5, 0.5, &M) == -11);
    CK(blend_matrix(BLEND_DARKEN, 0.5, 0.5, 0.5, &M) == -11);
    CK(blend_matrix(BLEND_DODGE, 0.5, 0.5, 0.5, &M) == -11);

    /* 3. 连乘矩阵 == 逐层前向（不同顺序结果一般不同）。 */
    {
        BlendLayer L[3] = {
            {BLEND_MULTIPLY, 0.8, 0.7, 0.6},
            {BLEND_SCREEN,   0.2, 0.3, 0.1},
            {BLEND_EXCLUSION,0.4, 0.5, 0.6},
        };
        int order[3] = {0, 1, 2};
        BMat P; int init = 0;
        for (int k = 0; k < 3; ++k) {
            BMat Mk;
            CK(blend_matrix(L[order[k]].mode, L[order[k]].r, L[order[k]].g,
                            L[order[k]].b, &Mk) == 0);
            if (!init) { P = Mk; init = 1; } else { BMat R = bmat_mul(&Mk, &P); P = R; }
        }
        double base[3] = {0.9, 0.5, 0.2}, got[3], ref[3];
        bmat_apply(&P, base, got);
        ref_forward(L, order, 3, base, ref);
        for (int ch = 0; ch < 3; ++ch) CK(fabs(fmin(1, fmax(0, got[ch])) - ref[ch]) < 1e-12);
    }

    /* 4. 可逆性判定。 */
    {
        BlendLayer singular = {BLEND_NORMAL, 0.3, 0.4, 0.5};
        int pivot = -2;
        CK(blend_invertible(&singular, &pivot) == -10 && pivot == 0);
        BlendLayer mul0 = {BLEND_MULTIPLY, 0.0, 0.5, 0.5};
        CK(blend_invertible(&mul0, &pivot) == -10 && pivot == 0);
        BlendLayer scr1 = {BLEND_SCREEN, 0.5, 1.0, 0.5};
        CK(blend_invertible(&scr1, &pivot) == -10 && pivot == 1);
        BlendLayer exc = {BLEND_EXCLUSION, 0.5, 0.5, 0.5};
        CK(blend_invertible(&exc, &pivot) == -10);
        BlendLayer ok = {BLEND_PLUS, 0.1, 0.2, 0.3};
        CK(blend_invertible(&ok, &pivot) == 0);
        BlendLayer nonlinear = {BLEND_DARKEN, 0.3, 0.3, 0.3};
        CK(blend_invertible(&nonlinear, &pivot) == -11);
    }

    /* 5. 反解：3 个仿射可逆层，目标取某个"真序"的输出，必须反解回该序且近零色差。 */
    {
        BlendLayer L[3] = {
            {BLEND_MULTIPLY, 0.8, 0.7, 0.6},
            {BLEND_PLUS,     0.05, 0.1, 0.02},
            {BLEND_SCREEN,   0.2, 0.3, 0.1},
        };
        double base[3] = {0.9, 0.5, 0.2};
        int true_order[3] = {2, 0, 1};
        double target[3];
        ref_forward(L, true_order, 3, base, target);
        BlendSolution sol;
        int single = -1, bad_step = -2, bad_code = 0;
        int rc = blend_solve(L, 3, base, target, 0.5, &sol, &single, &bad_step, &bad_code);
        CK(rc == 0);
        CK(single == 0);
        CK(memcmp(sol.order, true_order, sizeof(true_order)) == 0);
        CK(sol.delta < 1e-9);
        /* 与独立暴力参照的最优解一致。 */
        int ref_order[3]; double ref_d, ref_rgb[3];
        ref_solve(L, 3, base, target, ref_order, &ref_d, ref_rgb);
        CK(memcmp(sol.order, ref_order, sizeof(ref_order)) == 0);
        CK(fabs(sol.delta - ref_d) < 1e-9);
        for (int ch = 0; ch < 3; ++ch) CK(fabs(sol.achieved[ch] - ref_rgb[ch]) < 1e-12);
        CK(sol.tried == 6);  /* 3! 个排列必须全部枚举 */
        CK(sol.elapsed_ns >= 0);
    }

    /* 5b. 4 层时枚举 4! = 24 个排列。 */
    {
        BlendLayer L[4] = {
            {BLEND_MULTIPLY, 0.85, 0.75, 0.65},
            {BLEND_SCREEN,   0.15, 0.25, 0.10},
            {BLEND_EXCLUSION,0.30, 0.40, 0.20},
            {BLEND_PLUS,     0.02, 0.03, 0.04},
        };
        double base[3] = {0.9, 0.5, 0.2}, target[3] = {0.5, 0.5, 0.5};
        BlendSolution sol;
        int single = 0, bad_step = -1, bad_code = 0;
        int rc = blend_solve(L, 4, base, target, 200.0, &sol, &single, &bad_step, &bad_code);
        CK(rc == 0 && sol.tried == 24 && single == 0);
    }

    /* 6. 不可逆拒绝：指出具体层与错误码（非线性 / 奇异）。 */
    {
        BlendLayer L[3] = {
            {BLEND_MULTIPLY, 0.8, 0.7, 0.6},
            {BLEND_DODGE,    0.4, 0.4, 0.4},
            {BLEND_PLUS,     0.05, 0.1, 0.02},
        };
        double base[3] = {0.5, 0.5, 0.5}, target[3] = {0.3, 0.3, 0.3};
        BlendSolution sol;
        int single = 0, bad_step = -1, bad_code = 0;
        int rc = blend_solve(L, 3, base, target, 1.0, &sol, &single, &bad_step, &bad_code);
        CK(rc == -11 && bad_step == 1 && bad_code == -11);

        BlendLayer L2[3] = {
            {BLEND_MULTIPLY, 0.8, 0.7, 0.6},
            {BLEND_NORMAL,   0.4, 0.4, 0.4},
            {BLEND_PLUS,     0.05, 0.1, 0.02},
        };
        rc = blend_solve(L2, 3, base, target, 1.0, &sol, &single, &bad_step, &bad_code);
        CK(rc == -10 && bad_step == 1 && bad_code == -10);
    }

    /* 7. 单层：直接输出，标记 single，不搜索。 */
    {
        BlendLayer L[1] = {{BLEND_MULTIPLY, 0.5, 0.5, 0.5}};
        double base[3] = {0.8, 0.6, 0.4};
        double target[3];
        ref_forward(L, (int[]){0}, 1, base, target);
        BlendSolution sol;
        int single = 0, bad_step = -1, bad_code = 0;
        int rc = blend_solve(L, 1, base, target, 0.5, &sol, &single, &bad_step, &bad_code);
        CK(rc == 0 && single == 1 && sol.order[0] == 0 && sol.tried == 1);
        CK(sol.delta < 1e-12);
    }

    /* 8. 容差达不到时返回 -12（非对称源色保证两个排列互异且都远离目标）。 */
    {
        BlendLayer L[2] = {
            {BLEND_MULTIPLY, 0.8, 0.7, 0.6},
            {BLEND_SCREEN,   0.2, 0.3, 0.1},
        };
        double base[3] = {0.9, 0.5, 0.5}, target[3] = {0.0, 1.0, 0.0};
        BlendSolution sol;
        int single = 0, bad_step = -1, bad_code = 0;
        int rc = blend_solve(L, 2, base, target, 0.01, &sol, &single, &bad_step, &bad_code);
        CK(rc == -12);
        /* 放宽容差后应能返回最优顺序。 */
        rc = blend_solve(L, 2, base, target, 200.0, &sol, &single, &bad_step, &bad_code);
        CK(rc == 0);
    }

    /* 9. 前向合成与参照在含非仿射模式时也一致。 */
    {
        BlendLayer L[3] = {
            {BLEND_DIFFERENCE, 0.3, 0.4, 0.5},
            {BLEND_DARKEN,     0.8, 0.7, 0.6},
            {BLEND_DODGE,      0.2, 0.3, 0.1},
        };
        int order[3] = {0, 1, 2};
        double base[3] = {0.9, 0.5, 0.2}, got[3], ref[3];
        CK(blend_forward(L, order, 3, base, got) == 0);
        ref_forward(L, order, 3, base, ref);
        for (int ch = 0; ch < 3; ++ch) CK(fabs(got[ch] - ref[ch]) < 1e-12);
    }

    /* 10. 色差：同色为 0，异色为正且对称。 */
    {
        double c1[3] = {0.2, 0.4, 0.6}, c2[3] = {0.2, 0.4, 0.6}, c3[3] = {0.9, 0.1, 0.1};
        CK(blend_delta_e(c1, c2) < 1e-12);
        CK(blend_delta_e(c1, c3) > 1.0);
        CK(fabs(blend_delta_e(c1, c3) - blend_delta_e(c3, c1)) < 1e-12);
    }

    /* 11. 固定顺序回归：声明顺序 0..n-1 的 blend_forward 必须与独立公式逐值一致
     *     （关闭新求解逻辑后的老路径，结果不得漂移）。 */
    {
        BlendLayer L[4] = {
            {BLEND_MULTIPLY,  0.85, 0.75, 0.65},
            {BLEND_SCREEN,    0.15, 0.25, 0.10},
            {BLEND_EXCLUSION, 0.30, 0.40, 0.20},
            {BLEND_PLUS,      0.02, 0.03, 0.04},
        };
        int order[4] = {0, 1, 2, 3};
        double base[3] = {0.9, 0.5, 0.2}, got[3], ref[3];
        CK(blend_forward(L, order, 4, base, got) == 0);
        ref_forward(L, order, 4, base, ref);
        for (int ch = 0; ch < 3; ++ch) CK(fabs(got[ch] - ref[ch]) < 1e-15);
        /* 固定顺序结果与排列求解互不影响：求解器内部不修改输入层。 */
        BlendLayer Lcopy[4];
        memcpy(Lcopy, L, sizeof(L));
        BlendSolution sol;
        int single = 0, bad_step = -1, bad_code = 0;
        double target[3] = {0.5, 0.5, 0.5};
        CK(blend_solve(L, 4, base, target, 200.0, &sol, &single, &bad_step, &bad_code) == 0);
        CK(memcmp(L, Lcopy, sizeof(L)) == 0);
        double got2[3];
        CK(blend_forward(L, order, 4, base, got2) == 0);
        for (int ch = 0; ch < 3; ++ch) CK(got2[ch] == got[ch]);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
