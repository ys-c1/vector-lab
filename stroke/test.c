#include "stroke.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void corner3(SPoint *p, double deg) {
    double r = deg * M_PI / 180.0;
    p[0] = (SPoint){0, 0};
    p[1] = (SPoint){10, 0};
    p[2] = (SPoint){10 + 10*cos(r), 10*sin(r)};
}

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

    /* 关掉新逻辑：输出与原来的固定接法逐字节不变 */
    SPoint q[4] = {{0,0},{30,0},{30,20},{0,20}};
    SPoly o1, o2;
    CK(stroke_polyline(q, 4, 3.0, &o1) == 0);
    CK(stroke_polyline_ex(q, 4, 3.0, 0, 0.0, 0, NULL, &o2) == 0);
    CK(o1.count == o2.count &&
       memcmp(o1.pts, o2.pts, (size_t)o1.count * sizeof(SPoint)) == 0);

    /* 四组拐角：30/90/120/170 度，逐一检查接法、面积误差、耗时 */
    double degs[4] = {30, 90, 120, 170};
    double h = 1.0, err_max = 0.1 * h * h;
    for (int i = 0; i < 4; ++i) {
        SPoint c[3]; corner3(c, degs[i]);
        SJoinReport rep[1]; SPoly o;
        CK(stroke_polyline_ex(c, 3, 2.0, 1, err_max, 8, rep, &o) == 0);
        CK(rep[0].vertex == 1);
        CK(rep[0].err < err_max);                 /* 误差必须小于给定值 */
        CK(rep[0].seconds >= 0.0);
        double a = degs[i] * M_PI / 180.0;
        double sector = 0.5 * h * h * a;
        CK(fabs(rep[0].err_bevel - fabs(0.5*h*h*sin(a) - sector)) < 1e-12);
        CK(fabs(rep[0].err_round - fabs(0.5*h*h*8.0*sin(a/8.0) - sector)) < 1e-12);
        /* 8 段圆角的面积误差在三者中最小 */
        CK(rep[0].join == SJOIN_ROUND);
        CK(rep[0].err == rep[0].err_round);
    }

    /* 圆角退化为斜切（round_segs=1）时两者误差相同，固定选斜切 */
    {
        SPoint c[3]; corner3(c, 170);
        SJoinReport rep[1]; SPoly o;
        CK(stroke_polyline_ex(c, 3, 2.0, 1, 10.0, 1, rep, &o) == 0);
        CK(rep[0].err_bevel == rep[0].err_round);
        CK(rep[0].join == SJOIN_BEVEL);
    }

    /* 拐角为 0 度或 180 度：拒绝 */
    {
        SPoint straight[3] = {{0,0},{10,0},{20,0}};
        SPoint reverse[3] = {{0,0},{10,0},{4,0}};
        SPoly o;
        CK(stroke_polyline_ex(straight, 3, 2.0, 1, 0.1, 8, NULL, &o) == -3);
        CK(stroke_polyline_ex(reverse, 3, 2.0, 1, 0.1, 8, NULL, &o) == -4);
    }

    /* 误差阈值：给得过小则整体失败 */
    {
        SPoint c[3]; corner3(c, 30);
        SPoly o;
        CK(stroke_polyline_ex(c, 3, 2.0, 1, 1e-12, 64, NULL, &o) == -5);
    }

    /* 参数校验 */
    CK(stroke_polyline_ex(q, 4, 3.0, 1, 0.0, 8, NULL, &o2) == -1);
    CK(stroke_polyline_ex(q, 4, 3.0, 1, 0.1, 0, NULL, &o2) == -1);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
