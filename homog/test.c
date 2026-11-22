#include "homog.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 贴图角点：单位正方形，顺序 左上 右上 右下 左下 */
static const HPoint SRC[4] = {{0,0},{1,0},{1,1},{0,1}};

static void fit_case(const char *name, const HPoint dst[4], int expect_ok) {
    Homog h;
    HResidual r;
    int rc = homog_solve(SRC, dst, &h, &r);
    if (!expect_ok) {
        printf("[%s] rejected rc=%d\n", name, rc);
        CK(rc < 0);
        return;
    }
    CK(rc == 0);
    printf("[%s] rc=%d rms=%.3e max=%.3e px  per-corner: %.2e %.2e %.2e %.2e\n",
           name, rc, r.rms, r.maxerr, r.perr[0], r.perr[1], r.perr[2], r.perr[3]);
    CK(r.maxerr <= 0.5);                         /* 硬性指标 */
    for (int i = 0; i < 4; ++i) {
        HPoint q = homog_apply(h, SRC[i]);
        CK(fabs(q.x - dst[i].x) <= 0.5);
        CK(fabs(q.y - dst[i].y) <= 0.5);
    }
}

int main(void) {
    /* 1. 正方形（带平移，检验精确解残差 ~0） */
    HPoint sq[4] = {{10,20},{110,20},{110,120},{10,120}};
    fit_case("square", sq, 1);
    Homog h; HResidual r;
    CK(homog_solve(SRC, sq, &h, &r) == 0);
    CK(fabs(h.h11 - 100) < 1e-8 && fabs(h.h22 - 100) < 1e-8);
    CK(fabs(h.h13 - 10) < 1e-8 && fabs(h.h23 - 20) < 1e-8);
    CK(fabs(h.h31) < 1e-10 && fabs(h.h32) < 1e-10);   /* 无透视分量 */

    /* 2. 梯形（透视收缩，仿射做不到精确贴合） */
    HPoint trap[4] = {{30,10},{130,10},{110,110},{50,110}};
    fit_case("trapezoid", trap, 1);

    /* 3. 极端斜置：强烈透视 + 大坐标 */
    HPoint skew[4] = {{5,5},{400,120},{60,380},{-120,260}};
    fit_case("extreme-skew", skew, 1);

    /* 4a. 退化共线：四点压在 y=x 附近（含轻微数值扰动，仍应判共线） */
    HPoint line4[4] = {{0,0},{10,10.0000000001},{20,20},{30,29.9999999999}};
    int bp[2] = {-1,-1};
    CK(homog_check(line4, 4, bp) == -7);
    printf("[collinear] bad pair=(%d,%d)\n", bp[0], bp[1]);
    CK(bp[0] >= 0 && bp[1] >= 0 && bp[0] != bp[1]);
    fit_case("collinear", line4, 0);

    /* 4b. 角点重合：第 1、2 点相同 */
    HPoint dup[4] = {{10,10},{10,10},{110,120},{12,110}};
    bp[0] = bp[1] = -1;
    CK(homog_check(dup, 4, bp) == -6);
    printf("[coincident] bad pair=(%d,%d)\n", bp[0], bp[1]);
    CK((bp[0] == 1 && bp[1] == 0) || (bp[0] == 0 && bp[1] == 1));
    fit_case("coincident", dup, 0);

    /* 4c. 另一对重合（第 3、4 点），检验指出的点对正确 */
    HPoint dup2[4] = {{0,0},{100,0},{100,100},{100,100}};
    bp[0] = bp[1] = -1;
    CK(homog_check(dup2, 4, bp) == -6);
    printf("[coincident2] bad pair=(%d,%d)\n", bp[0], bp[1]);
    CK((bp[0] == 2 && bp[1] == 3) || (bp[0] == 3 && bp[1] == 2));

    /* 5. DLT 与精化：精化只能改善残差，不会变差 */
    Homog hd; HResidual rd, rr;
    CK(homog_solve_dlt(SRC, trap, &hd) == 0);
    homog_residual(SRC, trap, &hd, &rd);
    Homog hr = hd;
    CK(homog_refine(SRC, trap, &hr, &rr) == 0);
    CK(rr.rms <= rd.rms + 1e-15);

    /* 6. 带噪声输入（模拟手调角点），最小二乘仍须压到 0.5px 内 */
    HPoint noisy[4] = {{30.3,9.8},{130.2,10.4},{109.7,109.6},{50.4,110.2}};
    fit_case("noisy", noisy, 1);

    /* 7. 开关：关闭透视后为纯仿射（h31=h32=0），重新开启恢复 */
    homog_set_perspective(0);
    CK(homog_perspective_enabled() == 0);
    Homog ha;
    CK(homog_solve(SRC, sq, &ha, &r) == 0);     /* 正方形仿射可精确贴合 */
    CK(ha.h31 == 0.0 && ha.h32 == 0.0);
    /* 真梯形仿射贴不住：四角最大残差 2.5px，触发 0.5px 硬上限 -9 */
    HPoint realtrap[4] = {{60,30},{170,30},{150,170},{50,170}};
    HResidual ra;
    CK(homog_solve_affine(SRC, realtrap, &ha, &ra) == 0);
    CK(ra.maxerr > 0.5 && ra.maxerr < 3.0);
    CK(homog_solve(SRC, realtrap, &ha, &ra) == -9);
    homog_set_perspective(1);
    CK(homog_perspective_enabled() == 1);
    CK(homog_solve(SRC, trap, &h, &r) == 0);
    CK(fabs(h.h31) + fabs(h.h32) > 1e-6);     /* 梯形必然带透视分量 */
    CK(homog_solve(SRC, realtrap, &h, &r) == 0 && r.maxerr <= 0.5);

    /* 8. 空指针/点数保护 */
    CK(homog_check(0, 4, 0) == -1);
    CK(homog_check(sq, 3, 0) == -5);
    CK(homog_solve(0, sq, &h, 0) == -1);

    /* 9. 恒等映射 */
    Homog id = homog_identity();
    HPoint p = {3.5, -2.25}, q = homog_apply(id, p);
    CK(q.x == p.x && q.y == p.y);

    /* 10. 求逆：目标角点映回贴图角点、任意点往返一致、奇异拒绝 */
    HPoint skew2[4] = {{5,5},{400,120},{60,380},{-120,260}};
    Homog h2, ih;
    CK(homog_solve(SRC, skew2, &h2, 0) == 0);
    CK(homog_invert(&h2, &ih) == 0);
    for (int i = 0; i < 4; ++i) {
        HPoint back = homog_apply(ih, skew2[i]);
        CK(fabs(back.x - SRC[i].x) < 1e-9 && fabs(back.y - SRC[i].y) < 1e-9);
    }
    HPoint probes[3] = {{0.3, 0.7}, {1.2, -0.4}, {5.0, 5.0}};
    for (int k = 0; k < 3; ++k) {
        HPoint fwd = homog_apply(h2, probes[k]);
        HPoint back = homog_apply(ih, fwd);
        CK(fabs(back.x - probes[k].x) < 1e-9);
        CK(fabs(back.y - probes[k].y) < 1e-9);
    }
    Homog iid;
    CK(homog_invert(&id, &iid) == 0);
    CK(iid.h11 == 1.0 && iid.h22 == 1.0 && iid.h31 == 0.0 && iid.h32 == 0.0);
    Homog singular = {0, 0, 1, 0, 0, 2, 0, 0};   /* 行列式为 0 */
    CK(homog_invert(&singular, &iid) == -8);
    CK(homog_invert(0, &iid) == -1);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
