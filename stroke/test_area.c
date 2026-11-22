/* 目标面积反解线宽的单元测试（仅 -DSTROKE_AREA_FIT 下编译）。
 * 覆盖：四类正常路径、负宽度/零长度拒绝、目标不可达、自交标记、
 * 模型与独立实测的一致性。 */
#include "stroke_area.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* 直角开放折线：L=50，一个 90° 转角，cap=π，C=tan45°-π/4
     * 目标面积 1200 应落在模型分支。 */
    SPoint L[3] = {{0,0},{30,0},{30,20}};
    SAreaFit f;
    CK(stroke_area_fit(L, 3, 0, 1200.0, 0.01, 400.0, &f) == STROKE_AREA_OK);
    CK(f.rel_err <= 0.01);
    CK(f.width > 0.0 && isfinite(f.width));
    CK(f.closed == 0);
    CK(f.self_intersecting == 0);
    CK(f.self_seg_a == -1);
    CK(f.used_measurement == 0);          /* 常规路径走二次模型 */
    CK(f.model_width > 0.0);
    printf("L 开放: w=%.6f cov=%.4f rel=%.2e model=%.6f %.3fms\n",
           f.width, f.coverage, f.rel_err, f.model_width, f.ms);

    /* 模型面积必须随宽度严格递增，才能在面积上单调定位宽度。 */
    double l1, k1, l2, k2, a1, a2;
    a1 = stroke_area_model(L, 3, 0, 5.0, &l1, &k1);
    a2 = stroke_area_model(L, 3, 0, 10.0, &l2, &k2);
    CK(a1 > 0.0 && a2 > a1);
    CK(fabs(l1 - 50.0) < 1e-9);
    CK(fabs(l2 - l1) < 1e-12 && fabs(k2 - k1) < 1e-12);
    /* 手算：L*w + (π - C) w²/4，C = 1 - π/4 */
    double C = 1.0 - 3.14159265358979323846 / 4.0;
    double want = 50.0 * 5.0 + (3.14159265358979323846 - C) * 25.0 / 4.0;
    CK(fabs(a1 - want) < 1e-9);

    /* 实测面积与模型在无重叠区间内应吻合（误差远小于 1%）。 */
    double meas = stroke_area_measure(L, 3, 0, 5.0, 0.0);
    CK(meas > 0.0);
    CK(fabs(meas - a1) / a1 < 0.005);
    printf("模型 %.6f vs 实测 %.6f (相对 %.4f%%)\n", a1, meas, fabs(meas-a1)/a1*100);

    /* 负宽度：w_min < 0 拒绝，且指认宽度参数。 */
    CK(stroke_area_fit(L, 3, 0, 1200.0, -1.0, 400.0, &f) == STROKE_AREA_E_WIDTH);
    CK(stroke_area_fit(L, 3, 0, 1200.0, 5.0, 5.0, &f) == STROKE_AREA_E_WIDTH);
    CK(stroke_area_fit(L, 3, 0, 1200.0, 10.0, 5.0, &f) == STROKE_AREA_E_WIDTH);
    printf("负宽度: %s\n", stroke_area_strerror(STROKE_AREA_E_WIDTH));

    /* 路径长度为零：全重合点，必须拒绝并指出零长度。 */
    SPoint zero[3] = {{7,7},{7,7},{7,7}};
    CK(stroke_area_fit(zero, 3, 0, 100.0, 0.01, 400.0, &f) == STROKE_AREA_E_ZERO_LEN);
    printf("零长度: %s\n", stroke_area_strerror(STROKE_AREA_E_ZERO_LEN));

    /* 目标面积非正 / 非有限。 */
    CK(stroke_area_fit(L, 3, 0, 0.0, 0.01, 400.0, &f) == STROKE_AREA_E_TARGET);
    CK(stroke_area_fit(L, 3, 0, -5.0, 0.01, 400.0, &f) == STROKE_AREA_E_TARGET);
    CK(stroke_area_fit(L, 3, 0, NAN, 0.01, 400.0, &f) == STROKE_AREA_E_TARGET);

    /* 参数错误。 */
    CK(stroke_area_fit(NULL, 3, 0, 100.0, 0.01, 400.0, &f) == STROKE_AREA_E_ARG);
    CK(stroke_area_fit(L, 1, 0, 100.0, 0.01, 400.0, &f) == STROKE_AREA_E_ARG);
    CK(stroke_area_fit(L, 3, 0, 100.0, 0.01, 400.0, NULL) == STROKE_AREA_E_ARG);

    /* 目标面积超出 [w_min, w_max] 覆盖范围：不可达。 */
    CK(stroke_area_fit(L, 3, 0, 1e9, 0.01, 10.0, &f) == STROKE_AREA_E_NO_SOLUTION);
    printf("不可达: %s\n", stroke_area_strerror(STROKE_AREA_E_NO_SOLUTION));

    /* 自交：蝴蝶结开链，必须标记段对并仍能反解出面积达标的宽度。 */
    SPoint bow[4] = {{0,0},{30,20},{30,0},{0,20}};
    CK(stroke_area_fit(bow, 4, 0, 1200.0, 0.01, 400.0, &f) == STROKE_AREA_OK);
    CK(f.self_intersecting == 1);
    CK(f.self_seg_a >= 0 && f.self_seg_b > f.self_seg_a);
    CK(f.used_measurement == 1);          /* 自交一定走实测分支 */
    CK(f.rel_err <= 0.01);
    printf("自交: 段 %d x 段 %d @ (%.3f,%.3f) w=%.6f rel=%.2e\n",
           f.self_seg_a, f.self_seg_b, f.self_x, f.self_y, f.width, f.rel_err);

    /* 闭合矩形：L=120，cap=0，四个直角。 */
    SPoint rect[4] = {{0,0},{40,0},{40,20},{0,20}};
    CK(stroke_area_fit(rect, 4, 1, 1200.0, 0.01, 400.0, &f) == STROKE_AREA_OK);
    CK(f.closed == 1);
    CK(f.self_intersecting == 0);
    CK(f.rel_err <= 0.01);
    double lr, kr;
    double ar = stroke_area_model(rect, 4, 1, 10.0, &lr, &kr);
    CK(fabs(lr - 120.0) < 1e-9);
    CK(fabs(kr - (0.0 - (4.0 * (1.0 - 3.14159265358979323846 / 4.0))) / 4.0) < 1e-9);
    CK(ar > 0.0);
    printf("闭合矩形: w=%.6f cov=%.4f L=%.1f k=%.6f rel=%.2e %.3fms\n",
           f.width, f.coverage, lr, kr, f.rel_err, f.ms);

    /* 大面积（带宽重叠）：模型有意失效 -> 实测分支仍须满足 1%。 */
    SPoint U[4] = {{0,20},{0,0},{4,0},{4,20}};
    SAreaFit u;
    CK(stroke_area_fit(U, 4, 0, 300.0, 0.01, 400.0, &u) == STROKE_AREA_OK);
    CK(u.rel_err <= 0.01);
    printf("窄 U 大面积: w=%.6f cov=%.4f rel=%.2e meas=%d\n",
           u.width, u.coverage, u.rel_err, u.used_measurement);

    /* w_min = 0 合法：零宽度面积定义为零，反解正常。 */
    CK(stroke_area_measure(L, 3, 0, 0.0, 0.0) == 0.0);
    SAreaFit z;
    CK(stroke_area_fit(L, 3, 0, 1200.0, 0.0, 400.0, &z) == STROKE_AREA_OK);
    CK(z.rel_err <= 0.01);

    /* 期望面积随目标单调递增。 */
    double prev = 0.0;
    for (int i = 1; i <= 5; ++i) {
        double tgt = 300.0 * i;
        CK(stroke_area_fit(L, 3, 0, tgt, 0.01, 400.0, &f) == STROKE_AREA_OK);
        CK(f.rel_err <= 0.01);
        CK(f.width > prev);
        prev = f.width;
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
