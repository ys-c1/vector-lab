#include "clip.h"
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
    CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
    CPoly out;
    CK(clip_convex(poly,4,win,4,&out) == 0);
    CK(out.count == 4);
    CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);
    CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
    CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);
    CK(clip_convex(poly,2,win,4,&out) == -1);

    /* 顺时针窗口：必须与逆时针得到相同结果，不能把内部裁掉 */
    CPoint cw[4] = {{2,2},{2,8},{8,8},{8,2}};
    CK(clip_convex(poly,4,cw,4,&out) == 0);
    CK(out.count == 4);
    CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);

    /* 退化窗口（相邻顶点重合）：拒绝而不是静默产出 */
    CPoint degen[5] = {{0,0},{10,0},{10,0},{10,10},{0,10}};
    CK(clip_convex(poly,4,degen,5,&out) == -2);

    /* 大顶点多边形：结果不再受固定 64 点截断 */
    CPoint big[120];
    for (int i = 0; i < 120; ++i) {
        double a = 2.0 * M_PI * i / 120.0;
        big[i].x = 100.0 * cos(a);
        big[i].y = 100.0 * sin(a);
    }
    CPoint wb[4] = {{-40,-40},{60,-40},{60,50},{-40,50}};
    CK(clip_convex(big,120,wb,4,&out) == 0);
    CK(out.count == 4);   /* 窗口完全在圆内：只剩窗口四角 */
    CPoint wc[4] = {{0,0},{200,0},{200,200},{0,200}};
    CK(clip_convex(big,120,wc,4,&out) == 0);
    CK(out.count > 8);    /* 窗口切过圆周：旧的 62 点静默截断会在这里丢点 */

    /* 工作内存随输入规模线性，且有显式字节接口可做四倍预算 */
    CK(clip_working_bytes(120) == 2 * 121 * (int)sizeof(CPoint));
    CK(clip_working_bytes(2) == 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
