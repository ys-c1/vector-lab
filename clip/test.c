#include "clip.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int near(double a, double b, double tol) { return fabs(a - b) <= tol; }

int main(void) {
    CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
    CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
    CPoly out;
    CK(clip_convex(poly,4,win,4,&out) == 0);
    CK(out.count == 4);
    CK(near(fabs(clip_area(&out)), 36.0, 1e-9));

    CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
    CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);

    CK(clip_convex(poly,2,win,4,&out) == -1);
    CK(clip_convex(poly,4,win,2,&out) == -1);

    /* 顺时针窗口与逆时针窗口必须给出同一结果（旧实现返回空多边形）。 */
    CPoint cw[4] = {{2,8},{8,8},{8,2},{2,2}};
    CPoly a, b;
    CK(clip_convex(poly,4,win,4,&a) == 0);
    CK(clip_convex(poly,4,cw,4,&b) == 0);
    CK(a.count == b.count);
    CK(near(fabs(clip_area(&a)), fabs(clip_area(&b)), 1e-12));
    for (int i = 0; i < a.count; ++i) {
        int hit = 0;
        for (int j = 0; j < b.count; ++j)
            if (near(a.pts[i].x, b.pts[j].x, 1e-12) && near(a.pts[i].y, b.pts[j].y, 1e-12)) hit = 1;
        CK(hit);
    }

    /* 退化窗口：相邻顶点重合、零面积。 */
    CPoint dup[4] = {{2,2},{2,2},{8,8},{2,8}};
    CK(clip_convex(poly,4,dup,4,&out) == -2);
    CPoint flat[3] = {{2,2},{8,2},{5,2}};
    CK(clip_convex(poly,4,flat,3,&out) == -2);

    /* 连续两刀与先合并窗口再裁必须在浮点误差内一致。 */
    CPoint w1[4] = {{-4,-4},{12,-4},{12,7},{-4,7}};
    CPoint w2[4] = {{1,-1},{9,-1},{9,6},{1,6}};
    CK(clip_convex(poly,4,w1,4,&a) == 0);
    CK(clip_convex(a.pts,a.count,w2,4,&b) == 0);
    CPoly w12;
    CK(clip_convex(w1,4,w2,4,&w12) == 0);
    CK(clip_convex(poly,4,w12.pts,w12.count,&out) == 0);
    double two = fabs(clip_area(&b)), one = fabs(clip_area(&out));
    CK(near(two, one, 1e-9 * (one > 1.0 ? one : 1.0)));

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
