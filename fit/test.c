#include "src/fit.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static FTransform rot_t(double r, double tx, double ty) {
    FTransform t = {r, tx, ty, 1.0};
    return t;
}

/* 用一个已知变换把源四边形的四个角点映射成“观测落点”，可叠加扰动 */
static void make_group(const FPoint *src, int n, FTransform gt,
                       const FPoint *noise, FPoint *dst) {
    for (int i = 0; i < n; ++i) {
        FPoint q = fit_apply(gt, src[i]);
        dst[i].x = q.x + (noise ? noise[i].x : 0.0);
        dst[i].y = q.y + (noise ? noise[i].y : 0.0);
    }
}

static const FBounds strict = { 0.9, 1.1, 0.6, 5000.0, 0.5 };

int main(void) {
    /* 单位正方形四角 */
    FPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};

    /* 1) 精确对应：两种求解器都应收敛到真值，残差为机器精度量级 */
    FPoint exact[4];
    FTransform gt = rot_t(0.35, 120.0, -60.0);
    make_group(sq, 4, gt, NULL, exact);
    FFitResult r1 = fit_solve_linear(sq, exact, 4, &strict);
    CK(r1.status == FIT_OK);
    CK(fabs(r1.tr.rot - gt.rot) < 1e-9);
    CK(fabs(r1.tr.tx - gt.tx) < 1e-8);
    CK(fabs(r1.tr.ty - gt.ty) < 1e-8);
    CK(r1.max_residual < 1e-9);
    for (int i = 0; i < 4; ++i) CK(r1.per_corner[i] < 1e-9);

    FFitResult t1 = fit_solve_trial(sq, exact, 4, &strict);
    CK(t1.status == FIT_OK);
    CK(fabs(t1.tr.rot - gt.rot) < 1e-6);
    CK(fabs(t1.tr.tx - gt.tx) < 1e-5);
    CK(fabs(t1.tr.ty - gt.ty) < 1e-5);

    /* 2) 小扰动（< 半像素）：接受，逐角残差可复算 */
    FPoint small_noise[4] = {{0.20,-0.15},{-0.10,0.25},{0.15,0.10},{-0.20,-0.10}};
    FPoint noisy[4];
    make_group(sq, 4, gt, small_noise, noisy);
    FFitResult r2 = fit_solve_linear(sq, noisy, 4, &strict);
    CK(r2.status == FIT_OK);
    CK(r2.max_residual <= 0.5);
    double recomputed = 0.0;
    for (int i = 0; i < 4; ++i) {
        FPoint q = fit_apply(r2.tr, sq[i]);
        double e = hypot(q.x - noisy[i].x, q.y - noisy[i].y);
        CK(fabs(e - r2.per_corner[i]) < 1e-12);
        if (e > recomputed) recomputed = e;
    }
    CK(fabs(recomputed - r2.max_residual) < 1e-12);
    CK(r2.rms_residual <= r2.max_residual);

    /* 3) 大扰动（> 半像素）：拒绝并报 FIT_E_RESIDUAL，仍给出解与残差 */
    FPoint big_noise[4] = {{1.5,0.0},{0.0,0.0},{0.0,0.0},{0.0,0.0}};
    FPoint badfit[4];
    make_group(sq, 4, gt, big_noise, badfit);
    FFitResult r3 = fit_solve_linear(sq, badfit, 4, &strict);
    CK(r3.status == FIT_E_RESIDUAL);
    CK(r3.max_residual > 0.5);

    /* 4a) 退化（秩亏）：源角点全部重合 → 转角不可观测，冲突角点有明确下标 */
    FPoint same[4] = {{5,5},{5,5},{5,5},{5,5}};
    FPoint dst_split[4] = {{0,0},{1,0},{1,1},{0,1}};
    FFitResult r4a = fit_solve_linear(same, dst_split, 4, &strict);
    CK(r4a.status == FIT_E_DEGENERATE);
    CK(r4a.conflict_a >= 0 && r4a.conflict_b >= 0);
    CK(r4a.conflict_a != r4a.conflict_b);
    CK(strlen(r4a.conflict_axis[0]) > 0);

    /* 4b) 源角点共线在该模型下并不秩亏：数据不相容时表现为残差超差。
     *     让相邻落点间距整体保持 4（隐含缩放≈1，不触发越界），仅令中间点
     *     横向错位 1.2px，制造任何刚性变换都消不掉的不相容。 */
    FPoint line[4] = {{0,0},{4,0},{8,0},{12,0}};
    FPoint dst_line[4] = {{0,0},{4,0},{8,1.2},{12,0}};
    FFitResult r4b = fit_solve_linear(line, dst_line, 4, &strict);
    CK(r4b.status == FIT_E_RESIDUAL);
    CK(r4b.max_residual > 0.5);
    CK(r4b.tr.scale_unused > 0.9 && r4b.tr.scale_unused < 1.1);

    /* 4c) 共线但数据相容（真实刚性变换生成）：应正常解出，不退化 */
    FPoint dst_line_ok[4];
    FTransform gt_line = rot_t(0.2, 7.0, -3.0);
    for (int i = 0; i < 4; ++i) dst_line_ok[i] = fit_apply(gt_line, line[i]);
    FFitResult r4c = fit_solve_linear(line, dst_line_ok, 4, &strict);
    CK(r4c.status == FIT_OK);
    CK(fabs(r4c.tr.rot - gt_line.rot) < 1e-9);

    /* 5) 越界：真实缩放为 3，超出 [0.9,1.1] → FIT_E_BOUNDS 拒绝 */
    FPoint big[4];
    for (int i = 0; i < 4; ++i) {
        big[i].x = sq[i].x * 3.0;
        big[i].y = sq[i].y * 3.0;
    }
    FFitResult r5 = fit_solve_linear(sq, big, 4, &strict);
    CK(r5.status == FIT_E_BOUNDS);
    CK(fabs(r5.tr.scale_unused - 3.0) < 1e-9);

    /* 5b) 转角越界：真实转角 1.2 rad > 上限 0.6 */
    FPoint rot_bad[4];
    FTransform gt_bigrot = rot_t(1.2, 0.0, 0.0);
    make_group(sq, 4, gt_bigrot, NULL, rot_bad);
    FFitResult r5b = fit_solve_linear(sq, rot_bad, 4, &strict);
    CK(r5b.status == FIT_E_BOUNDS);

    /* 6) 入参非法 */
    FFitResult r6 = fit_solve_linear(NULL, exact, 4, &strict);
    CK(r6.status == FIT_E_INPUT);
    FFitResult r6b = fit_solve_linear(sq, exact, 1, &strict);
    CK(r6b.status == FIT_E_INPUT);

    /* 7) 编译期开关：fit_solve 必须与当前编译模式对应的求解器一致 */
    FFitResult rd = fit_solve(sq, noisy, 4, &strict);
#ifdef FIT_NO_LINEAR
    FFitResult rt = fit_solve_trial(sq, noisy, 4, &strict);
#else
    FFitResult rt = fit_solve_linear(sq, noisy, 4, &strict);
#endif
    CK(rd.status == rt.status);
    CK(fabs(rd.tr.rot - rt.tr.rot) < 1e-12);
    CK(fabs(rd.max_residual - rt.max_residual) < 1e-12);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
