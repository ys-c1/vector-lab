#include "stroke.h"
#include "adaptive.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    SPoint p[3] = {{0,0},{10,0},{10,10}};
    SPoly out;
    CK(stroke_polyline(p, 3, 2.0, &out) == 0);
    CK(out.count == 6);
    CK(out.pts[0].y > 0.9 && out.pts[0].y < 1.1);
    CK(stroke_polyline(p, 1, 2.0, &out) == -1);
    CK(stroke_polyline(p, 3, 0.0, &out) == -1);
    SPoint bad[2] = {{0,0},{0,0}};
    CK(stroke_polyline(bad, 2, 1.0, &out) == -2);

    /* 曲率：直线段标零 */
    SPoint line[4] = {{0,0},{10,0},{20,0},{30,0}};
    double kap[8]; int fl[8];
    CK(stroke_curvature(line, 4, kap, fl) == 0);
    CK(fl[0] == SCURV_ZERO && fl[1] == SCURV_ZERO && fl[2] == SCURV_ZERO);
    CK(kap[0] == 0.0 && kap[1] == 0.0 && kap[2] == 0.0);

    /* 曲率：圆弧段接近 1/R */
    SPoint arc[9];
    for (int i = 0; i < 9; ++i) {
        double a = i * 10.0 * PI / 180.0;
        arc[i].x = 50.0 * cos(a); arc[i].y = 50.0 * sin(a);
    }
    CK(stroke_curvature(arc, 9, kap, fl) == 0);
    CK(fabs(kap[4]) > 0.0198 && fabs(kap[4]) < 0.0202);
    CK(fl[4] == SCURV_OK);

    /* 曲率：180° 回折标无穷 */
    SPoint fold[3] = {{0,0},{10,0},{5,0}};
    CK(stroke_curvature(fold, 3, kap, fl) == 0);
    CK(fl[0] == SCURV_INF && fl[1] == SCURV_INF);
    CK(isinf(kap[0]) && isinf(kap[1]));

    /* 反解：直线段宽度等于目标，比值 1 */
    double w[8]; SWidthReport rep;
    CK(stroke_widths_solve(line, 4, 2.0, w, &rep) == 0);
    CK(w[0] == 2.0 && w[1] == 2.0 && w[2] == 2.0);
    CK(rep.max_width / rep.min_width <= 1.2 + 1e-12);
    CK(rep.max_abs_deviation < 1e-12);
    CK(rep.seconds >= 0.0);

    /* 反解：急弯被夹取，最粗/最细 <= 1.2 */
    SPoint tight[9];
    for (int i = 0; i < 9; ++i) {
        double a = i * 20.0 * PI / 180.0;
        tight[i].x = 3.0 * cos(a); tight[i].y = 3.0 * sin(a);
    }
    CK(stroke_widths_solve(tight, 9, 2.0, w, &rep) == 0);
    CK(rep.max_width / rep.min_width <= 1.2 + 1e-12);
    CK(rep.min_width >= 2.0 / sqrt(1.2) - 1e-12);
    CK(rep.max_width <= 2.0 * sqrt(1.2) + 1e-12);

    /* 反解：回折无穷段落到夹取下界且被标记 */
    CK(stroke_widths_solve(fold, 3, 2.0, w, &rep) == 0);
    CK(rep.seg[0].flag == SCURV_INF && rep.seg[1].flag == SCURV_INF);
    CK(fabs(w[0] - 2.0 / sqrt(1.2)) < 1e-12);
    CK(isnan(rep.seg[0].deviation));

    /* 拒绝：目标宽度非正 */
    CK(stroke_widths_solve(line, 4, -1.0, w, &rep) == -1);
    CK(stroke_widths_solve(line, 4, 0.0, w, &rep) == -1);

    /* 拒绝：段宽为负，指出段号 */
    double wneg[3] = {2.0, -0.5, 2.0};
    int err_seg = -1;
    CK(stroke_polyline_varying(line, 4, wneg, &out, &err_seg) == -3);
    CK(err_seg == 1);

    /* 变宽描边正常路径 */
    double wok[3] = {2.0, 1.9, 2.0};
    CK(stroke_polyline_varying(line, 4, wok, &out, NULL) == 0);
    CK(out.count == 8);

    /* 关掉新逻辑：与原始等宽路径逐字节一致 */
    SPoint demo[4] = {{0,0},{30,0},{30,20},{0,20}};
    SPoly a, b;
    CK(stroke_polyline(demo, 4, 3.0, &a) == 0);
    CK(stroke_outline(demo, 4, 3.0, 0, &b, NULL) == 0);
    CK(a.count == b.count);
    CK(memcmp(a.pts, b.pts, sizeof(SPoint) * (size_t)a.count) == 0);

    /* 打开新逻辑：统一入口出报告 */
    CK(stroke_outline(tight, 9, 2.0, 1, &b, &rep) == 0);
    CK(rep.nseg == 8);
    CK(b.count == 18);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
