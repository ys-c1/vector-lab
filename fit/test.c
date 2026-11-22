#include "fit.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static FPoint cubic_at(const FPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    FPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

int main(void) {
    FFit f;

    /* 点位少于 2：拒绝 */
    FPoint one[1] = {{1, 2}};
    CK(fit_bezier(one, 1, &f) == -1);
    CK(fit_bezier(one, 0, &f) == -1);
    CK(fit_bezier(NULL, 3, &f) == -2);
    CK(fit_bezier(one, FIT_MAX + 1, &f) == -2);

    /* 共线退化：控制点在弦的三分点，残差为 0 */
    FPoint col[5] = {{0, 0}, {3, 1}, {6, 2}, {9, 3}, {12, 4}};
    CK(fit_bezier(col, 5, &f) == 0);
    CK(f.degenerate == 1);
    CK(f.max_err <= 1e-9);
    CK(fabs(f.ctrl[1].x - 4.0) < 1e-9 && fabs(f.ctrl[1].y - 4.0 / 3.0) < 1e-9);
    CK(fabs(f.ctrl[2].x - 8.0) < 1e-9 && fabs(f.ctrl[2].y - 8.0 / 3.0) < 1e-9);

    /* 两点也是共线：直线化 */
    FPoint two[2] = {{0, 0}, {9, 0}};
    CK(fit_bezier(two, 2, &f) == 0);
    CK(f.degenerate == 1);
    CK(fabs(f.ctrl[1].x - 3.0) < 1e-9 && fabs(f.ctrl[2].x - 6.0) < 1e-9);

    /* 全部重合：曲线缩为点 */
    FPoint same[3] = {{5, 5}, {5, 5}, {5, 5}};
    CK(fit_bezier(same, 3, &f) == 0);
    CK(f.degenerate == 2);
    CK(f.max_err == 0.0);
    CK(f.ctrl[0].x == 5.0 && f.ctrl[3].x == 5.0);

    /* 从已知三次贝塞尔采样：反解应回到原控制点附近，残差远小于半像素 */
    FPoint src[4] = {{0, 0}, {30, 60}, {70, -20}, {100, 40}};
    FPoint pts[8];
    for (int i = 0; i < 8; ++i) pts[i] = cubic_at(src, (double)i / 7);
    CK(fit_bezier(pts, 8, &f) == 0);
    CK(f.degenerate == 0);
    CK(f.max_err <= 0.5);
    CK(fabs(f.ctrl[1].x - 30.0) < 0.5 && fabs(f.ctrl[1].y - 60.0) < 0.5);
    CK(fabs(f.ctrl[2].x - 70.0) < 0.5 && fabs(f.ctrl[2].y + 20.0) < 0.5);

    /* 端点必须精确穿过 */
    CK(f.ctrl[0].x == pts[0].x && f.ctrl[0].y == pts[0].y);
    CK(f.ctrl[3].x == pts[7].x && f.ctrl[3].y == pts[7].y);

    /* 任意温和点位：单段可能超差，分段样条必须压进半像素 */
    FPoint hand[6] = {{0, 0}, {15, 30}, {40, 42}, {60, 38}, {80, 20}, {95, 5}};
    FFit segs[FIT_MAX_SEG];
    int nseg = fit_spline(hand, 6, FIT_TOL, segs, FIT_MAX_SEG);
    CK(nseg >= 1);
    for (int s = 0; s < nseg; ++s) CK(segs[s].max_err <= FIT_TOL);
    /* 相邻段首尾相接（C0 连续） */
    for (int s = 0; s + 1 < nseg; ++s) {
        CK(segs[s].ctrl[3].x == segs[s + 1].ctrl[0].x);
        CK(segs[s].ctrl[3].y == segs[s + 1].ctrl[0].y);
    }
    CK(fit_spline(hand, 1, FIT_TOL, segs, FIT_MAX_SEG) == -1);
    CK(fit_spline(hand, 6, 0.0, segs, FIT_MAX_SEG) == -2);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
