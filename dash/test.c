#include "dash.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    DPoint p[2] = {{0,0},{100,0}};
    DPoly out;
    CK(dash_polyline(p, 2, 10.0, 10.0, &out) == 0);
    CK(out.count > 80);
    CK(out.pts[0].x >= 0.0 && out.pts[0].x < 1.0);
    CK(dash_polyline(p, 2, 0.0, 10.0, &out) == -1);
    DPoint bad[2] = {{0,0},{0,0}};
    CK(dash_polyline(bad, 2, 5.0, 5.0, &out) == -2);

    /* 反解周期: 两组共线/折线/闭合/零长 */
    DashFit f;
    DPoint g2[3] = {{0,0},{60,0},{60,40}};                 /* 两段, L=100 */
    DPoint g4[5] = {{0,0},{30,0},{30,20},{50,20},{50,50}}; /* 四段, L=100 */
    DPoint gc[5] = {{0,0},{40,0},{40,40},{0,40},{0,0}};    /* 闭合环, L=160 */
    DPoint g0[5] = {{0,0},{0,0},{50,0},{50,0},{50,40}};    /* 零长x2, L=90 */
    const DPoint *gp[4] = {g2, g4, gc, g0};
    int gn[4] = {3, 5, 5, 5};
    int gd[4] = {0, 0, 0, 2};
    for (int g = 0; g < 4; ++g) {
        CK(dash_polyline_fit(gp[g], gn[g], 8.0, 6.0, 1, &f, &out) == 0);
        CK(f.period > 0.0);
        CK(f.end_phase >= 0.0 && f.end_phase <= f.on);       /* 终点落在实线段 */
        CK(f.end_dev <= 0.5 * f.on);                         /* 偏差不超过半线段 */
        CK(fabs(f.end_phase - 0.5 * f.on) < 1e-6);           /* 正好在中点 */
        CK(f.degenerate == gd[g]);                           /* 退化段单独统计 */
        CK(f.elapsed_us >= 0.0);
    }
    /* 已知解: L=100 on=10 off=10 -> m=5, period=100/5.25 */
    CK(dash_solve_period(100.0, 10.0, 10.0, &f) == 0);
    CK(fabs(f.period - 100.0 / 5.25) < 1e-9);
    CK(f.cycles == 5);
    /* 拒绝: 周期为零、总长不足一个周期 */
    CK(dash_solve_period(100.0, 0.0, 0.0, &f) == -3);
    CK(dash_solve_period(5.0, 10.0, 10.0, &f) == -4);
    DPoint zz[2] = {{3,3},{3,3}};
    CK(dash_polyline_fit(zz, 2, 8.0, 6.0, 1, &f, &out) == -4);
    CK(f.degenerate == 1);
    /* 关掉新逻辑: 与原手调路径逐点一致 */
    DPoly o1, o2;
    CK(dash_polyline(g4, 5, 8.0, 6.0, &o1) == 0);
    CK(dash_polyline_fit(g4, 5, 8.0, 6.0, 0, NULL, &o2) == 0);
    CK(o1.count == o2.count);
    CK(memcmp(o1.pts, o2.pts, sizeof(DPoint) * (size_t)o1.count) == 0);
    CK(dash_polyline_fit(bad, 2, 5.0, 5.0, 0, NULL, &o2) == -2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
