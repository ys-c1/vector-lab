/*
 * 四组混合链演示：
 *   A 全仿射可逆 3 层（multiply/plus/screen）—— 目标可精确命中
 *   B 全仿射可逆 4 层（含 exclusion）—— 24 排列搜索
 *   C 含非线性模式 color-dodge —— 拒绝，报告具体步骤
 *   D 含奇异模式 normal —— 拒绝，报告具体步骤
 * 另出单层（E）与固定顺序对照（F，新逻辑关/开两条路径逐字节一致）。
 *
 * BLEND_FIXED=1 时走老的固定顺序（层声明顺序），用于字节级回归。
 */
#include "src/blend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOL 1.0  /* CIE76 Delta E 容差 */

static void print_mat(const char *name, const BMat *M) {
    printf("  matrix %s:\n", name);
    for (int i = 0; i < 4; ++i) {
        printf("    [");
        for (int j = 0; j < 4; ++j) printf(" %8.4f", M->m[i][j]);
        printf(" ]\n");
    }
}

static void print_rgb(const char *tag, const double c[3]) {
    printf("%s (%.4f %.4f %.4f)\n", tag, c[0], c[1], c[2]);
}

static void report_group(const char *gid, const BlendLayer *L, int n,
                         const double base[3], const double target[3]) {
    printf("== group %s: %d layers, tol DeltaE %.2f ==\n", gid, n, TOL);
    for (int i = 0; i < n; ++i) {
        BMat M;
        int rc = blend_matrix(L[i].mode, L[i].r, L[i].g, L[i].b, &M);
        printf(" layer %d %s src(%.2f %.2f %.2f)\n",
               i, blend_name(L[i].mode), L[i].r, L[i].g, L[i].b);
        if (rc == 0) print_mat(blend_name(L[i].mode), &M);
        else printf("  matrix: NONE (non-affine, rc=%d)\n", rc);
    }
    print_rgb("  base  ", base);
    print_rgb("  target", target);

    BlendSolution sol;
    int single = 0, bad_step = -1, bad_code = 0;
    int rc = blend_solve(L, n, base, target, TOL, &sol, &single, &bad_step, &bad_code);
    if (rc == -11 || rc == -10) {
        printf("  REJECT at layer %d (%s): %s\n",
               bad_step, blend_name(L[bad_step].mode),
               rc == -11 ? "mode is non-affine, no matrix form"
                         : "matrix is singular (zero determinant), not invertible");
        printf("--\n");
        return;
    }
    if (rc == -12) {
        printf("  NO ORDER within tolerance; best DeltaE %.4f\n", sol.delta);
        printf("--\n");
        return;
    }
    if (rc != 0) {
        printf("  ERROR rc=%d\n", rc);
        printf("--\n");
        return;
    }
    if (single) printf("  SINGLE LAYER: direct output, no search\n");
    printf("  order  (bottom->top):");
    for (int i = 0; i < n; ++i) printf(" %d:%s", sol.order[i], blend_name(L[sol.order[i]].mode));
    printf("\n");
    print_rgb("  got   ", sol.achieved);
    printf("  DeltaE %.4f (limit %.2f) %s\n", sol.delta, TOL,
           sol.delta <= TOL ? "PASS" : "FAIL");
    printf("  permutations %d, time %ld ns (%.3f us)\n",
           sol.tried, sol.elapsed_ns, sol.elapsed_ns / 1000.0);
    printf("--\n");
}

/* 固定顺序路径：按声明顺序 0..n-1 前向（老逻辑）。 */
static void fixed_path(const char *gid, const BlendLayer *L, int n,
                       const double base[3]) {
    int order[BLEND_MAX_LAYERS];
    for (int i = 0; i < n; ++i) order[i] = i;
    double out[3];
    blend_forward(L, order, n, base, out);
    printf("FIXED %s order:", gid);
    for (int i = 0; i < n; ++i) printf(" %d:%s", i, blend_name(L[i].mode));
    printf(" -> (%.6f %.6f %.6f)\n", out[0], out[1], out[2]);
}

int main(void) {
    int fixed_mode = getenv("BLEND_FIXED") != NULL;
    if (fixed_mode) printf("# BLEND_FIXED set: legacy fixed-order path\n");

    double base[3] = {0.90, 0.50, 0.20};

    /* A：3 层全可逆，目标 = 真序 (2,0,1) 的输出。 */
    BlendLayer A[3] = {
        {BLEND_MULTIPLY, 0.80, 0.70, 0.60},
        {BLEND_PLUS,     0.05, 0.10, 0.02},
        {BLEND_SCREEN,   0.20, 0.30, 0.10},
    };
    int ordA[3] = {2, 0, 1};
    double tgtA[3];
    blend_forward(A, ordA, 3, base, tgtA);

    /* B：4 层全可逆，含 exclusion（源色避开 0.5 奇异点）。 */
    BlendLayer B[4] = {
        {BLEND_MULTIPLY,  0.85, 0.75, 0.65},
        {BLEND_SCREEN,    0.15, 0.25, 0.10},
        {BLEND_EXCLUSION, 0.30, 0.40, 0.20},
        {BLEND_PLUS,      0.02, 0.03, 0.04},
    };
    int ordB[4] = {3, 1, 0, 2};
    double tgtB[3];
    blend_forward(B, ordB, 4, base, tgtB);

    /* C：含 color-dodge（非线性，无矩阵）。 */
    BlendLayer C[3] = {
        {BLEND_MULTIPLY, 0.80, 0.70, 0.60},
        {BLEND_DODGE,    0.40, 0.40, 0.40},
        {BLEND_PLUS,     0.05, 0.10, 0.02},
    };
    double tgtC[3] = {0.30, 0.35, 0.25};

    /* D：含 normal（a=0 奇异）。 */
    BlendLayer D[3] = {
        {BLEND_MULTIPLY, 0.80, 0.70, 0.60},
        {BLEND_NORMAL,   0.40, 0.40, 0.40},
        {BLEND_PLUS,     0.05, 0.10, 0.02},
    };
    double tgtD[3] = {0.30, 0.35, 0.25};

    /* E：单层，直接输出。 */
    BlendLayer E[1] = {{BLEND_MULTIPLY, 0.50, 0.50, 0.50}};
    double tgtE[3];
    int ordE[1] = {0};
    blend_forward(E, ordE, 1, base, tgtE);

    if (fixed_mode) {
        fixed_path("A", A, 3, base);
        fixed_path("B", B, 4, base);
        fixed_path("C", C, 3, base);
        fixed_path("D", D, 3, base);
        fixed_path("E", E, 1, base);
        return 0;
    }

    report_group("A", A, 3, base, tgtA);
    report_group("B", B, 4, base, tgtB);
    report_group("C", C, 3, base, tgtC);
    report_group("D", D, 3, base, tgtD);
    report_group("E", E, 1, base, tgtE);

    /* F：新逻辑关闭时固定顺序输出不变（同机对照行）。 */
    fixed_path("F", A, 3, base);
    return 0;
}
