#include "fit.h"
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* 1. 轴对齐矩形：零度即最优，无候选角可改善，按无差别处理且不旋转 */
    FPoint rect[4] = {{0,0},{20,0},{20,4},{0,4}};
    FitResult r;
    CK(fit_optimize(rect, 4, &r) == 0);
    CK(fabs(r.angle) < 1e-9);
    CK(fabs(r.area - 80.0) < 1e-9);
    CK(r.indifferent == 1);

    /* 2. 旋转了 30 度的长矩形：应反解回 -30 度（折叠后 pi/2 - pi/6 = pi/3） */
    double a = M_PI / 6.0, c = cos(a), s = sin(a);
    FPoint tilted[4];
    FPoint base[4] = {{-10,-1.5},{10,-1.5},{10,1.5},{-10,1.5}};
    for (int i = 0; i < 4; ++i) {
        tilted[i].x = base[i].x * c - base[i].y * s;
        tilted[i].y = base[i].x * s + base[i].y * c;
    }
    CK(fit_optimize(tilted, 4, &r) == 0);
    CK(fabs(r.angle - (M_PI / 2.0 - M_PI / 6.0)) < 1e-9);
    CK(fabs(r.area - 60.0) < 1e-7);
    CK(r.area < r.area0 * 0.9);                 /* 比零度小一成以上 */
    CK(fit_area_at(tilted, 4, r.angle) <= fit_area_at(tilted, 4, 0.0));

    /* 3. 菱形（旋转 45 度的正方形）：面积能降回一半 */
    FPoint diamond[4] = {{0,1},{1,0},{0,-1},{-1,0}};
    CK(fit_optimize(diamond, 4, &r) == 0);
    CK(fabs(r.angle - M_PI / 4.0) < 1e-9);
    CK(fabs(r.area - 2.0) < 1e-9);
    CK(fabs(r.area0 - 4.0) < 1e-9);
    CK(r.area < r.area0 * 0.9);

    /* 4. 正方形：无差别场景，任何角度面积都不更小 */
    FPoint sq[4] = {{0,0},{8,0},{8,8},{0,8}};
    CK(fit_optimize(sq, 4, &r) == 0);
    CK(r.indifferent == 1);
    CK(fabs(r.angle) < 1e-9);
    CK(fabs(r.area - 64.0) < 1e-9);
    CK(fit_area_at(sq, 4, M_PI / 4.0) >= r.area - 1e-9);

    /* 5. 三角形 */
    FPoint tri[3] = {{0,0},{10,0},{0,3}};
    CK(fit_optimize(tri, 3, &r) == 0);
    CK(fabs(r.angle) < 1e-9);

    /* 6. 角度越界拒绝：合法域为 [0, pi/2) */
    CK(fit_validate(0.0) == 0);
    CK(fit_validate(M_PI / 2.0 - 1e-12) == 0);
    CK(fit_validate(M_PI / 2.0) == -1);
    CK(fit_validate(-0.1) == -1);
    CK(fit_validate(M_PI) == -1);

    /* 7. 入参防御与退化点集：共线点集可真正转到面积 0，不属于无差别 */
    CK(fit_optimize(NULL, 4, &r) == -1);
    CK(fit_optimize(rect, 0, &r) == -1);
    FPoint line[3] = {{0,0},{1,1},{2,2}};
    CK(fit_optimize(line, 3, &r) == 0);
    CK(fabs(r.angle - M_PI / 4.0) < 1e-9 && r.area < 1e-9 && r.indifferent == 0);
    FPoint one[1] = {{3,3}};
    CK(fit_optimize(one, 1, &r) == 0 && r.indifferent == 1);

    /* 8. 周期性质：A(theta + pi/2) == A(theta) */
    CK(fabs(fit_area_at(tilted, 4, 0.3) - fit_area_at(tilted, 4, 0.3 + M_PI / 2.0)) < 1e-9);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
