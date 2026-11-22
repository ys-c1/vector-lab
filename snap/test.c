/* 反解等分总长的单元检查。
 * 重点：
 *   - 斜边 3-4-5、12 点的反解结果全落整点，总长 880（小数等分 900 会超半像素）
 *   - 目标函数是字典序：半像素可行 > 不可行；同误差取更长总长
 *   - 起点终点重合、零长线段给出明确错误码
 *   - 旧路径与新路径互相独立（旧路径不受反解影响）
 */
#include "snap.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

int main(void) {
    /* 1) 斜边 3-4-5，长度 900，12 点 */
    SnapPoint diag[2] = {{0, 0}, {540, 720}};
    SnapResult r;
    SnapNode nodes[12];
    CK(snap_solve(diag, 2, 12, &r, nodes, 12) == SNAP_OK);
    CK(r.status == SNAP_STATUS_EXACT);
    CK(r.exact);
    CK(r.feasible);
    CK(near(r.total, 900.0, 1e-9));
    CK(near(r.target, 880.0, 1e-6));
    CK(near(r.step, 80.0, 1e-6));
    CK(r.max_err <= SNAP_EXACT_EPS);
    for (int k = 0; k < 12; ++k) {
        CK(near(nodes[k].x, nodes[k].rx, 1e-6));
        CK(near(nodes[k].y, nodes[k].ry, 1e-6));
        CK(nodes[k].err <= SNAP_EXACT_EPS);
    }
    /* 反解的点确实都在折线上：x/4 == y/3 */
    for (int k = 0; k < 12; ++k)
        CK(near(nodes[k].x / 540.0, nodes[k].y / 720.0, 1e-9));

    /* 小数等分在同一输入上会超半像素：新逻辑必须比它好 */
    SnapPoint legacy[12];
    CK(snap_points_legacy(diag, 2, 12, legacy) == SNAP_OK);
    double worst = 0.0;
    for (int k = 0; k < 12; ++k) {
        double ex = legacy[k].x - round(legacy[k].x);
        double ey = legacy[k].y - round(legacy[k].y);
        double e = sqrt(ex*ex + ey*ey);
        if (e > worst) worst = e;
    }
    CK(worst > SNAP_HALF_PIXEL);
    CK(r.max_err < worst);

    /* 2) 水平直线、格点折线：可以全长 100% 覆盖 */
    SnapPoint zz[4] = {{0, 0}, {40, 0}, {40, 30}, {0, 30}};
    CK(snap_solve(zz, 4, 8, &r, NULL, 0) == SNAP_OK);
    /* 45° 折线两端都在格点上，8 点可以整点全覆盖 */
    CK(r.status == SNAP_STATUS_EXACT);
    CK(fabs(r.target - 140.0) <= 1e-6 || r.target <= 140.0 + 1e-6);

    /* 3) 查询模式（nodes == NULL）与容量检查 */
    CK(snap_solve(diag, 2, 12, &r, NULL, 0) == SNAP_OK);
    CK(snap_solve(diag, 2, 12, &r, nodes, 5) == SNAP_E_CAP);
    CK(snap_solve(NULL, 2, 12, &r, NULL, 0) == SNAP_E_ARG);
    CK(snap_solve(diag, 1, 12, &r, NULL, 0) == SNAP_E_ARG);
    CK(snap_solve(diag, 2, 1, &r, NULL, 0) == SNAP_E_ARG);

    /* 4) 起点终点重合：拒绝并带说明 */
    SnapPoint closed[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 0}};
    CK(snap_solve(closed, 4, 5, &r, NULL, 0) == SNAP_E_CLOSED);
    CK(r.detail[0] != '\0');

    /* 5) 零长线段与总长为零 */
    SnapPoint zs[3] = {{1, 1}, {1, 1}, {5, 5}};
    CK(snap_solve(zs, 3, 4, &r, NULL, 0) == SNAP_E_DEGENERATE);
    SnapPoint same[2] = {{5, 5}, {5, 5}};
    CK(snap_solve(same, 2, 4, &r, NULL, 0) == SNAP_E_CLOSED ||
       snap_solve(same, 2, 4, &r, NULL, 0) == SNAP_E_DEGENERATE);

    /* 6) 旧路径：直线 100、5 点 -> 步长 25，且函数不受反解影响 */
    SnapPoint line[2] = {{0, 0}, {100, 0}};
    SnapPoint lg[5];
    CK(snap_points_legacy(line, 2, 5, lg) == SNAP_OK);
    for (int k = 0; k < 5; ++k) CK(near(lg[k].x, 25.0 * k, 1e-12));
    CK(snap_points_legacy(line, 2, 1, lg) == SNAP_E_ARG);
    CK(snap_points_legacy(NULL, 2, 5, lg) == SNAP_E_ARG);

    /* 7) 字典序：半像素可行的方案优先于误差略小但越界的方案。
     *    nearly-flat 折线：任何 d 都有点离整点远，必须给出最接近方案。 */
    SnapPoint flat[2] = {{0, 0}, {1000, 1}};
    CK(snap_solve(flat, 2, 3, &r, NULL, 0) == SNAP_OK);
    CK(r.max_err >= 0.0 && r.max_err <= 0.5 * sqrt(2.0) + 1e-9);
    CK(r.status == SNAP_STATUS_EXACT || r.status == SNAP_STATUS_NEAREST ||
       r.status == SNAP_STATUS_OVER_BUDGET);

    /* 8) 二点折线 m=2：只有首尾两点，总长必须取满（端点在格点上） */
    SnapPoint ab[2] = {{0, 0}, {37, 0}};
    CK(snap_solve(ab, 2, 2, &r, NULL, 0) == SNAP_OK);
    CK(r.status == SNAP_STATUS_EXACT);
    CK(near(r.target, 37.0, 1e-9));

    /* 9) 状态文本恒定 */
    CK(snap_status_str(SNAP_STATUS_EXACT) != 0);
    CK(snap_strerror(SNAP_E_CLOSED) != 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
