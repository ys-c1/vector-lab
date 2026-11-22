#include "stroke.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

int main(void) {
    double degs[4] = {30, 90, 120, 170};
    printf("自适应接法：按面积误差选接法（斜接/斜切/圆角，圆角 8 段弦逼近）\n");
    for (int i = 0; i < 4; ++i) {
        double r = degs[i] * M_PI / 180.0;
        SPoint c[3] = {{0,0},{10,0},{10 + 10*cos(r), 10*sin(r)}};
        SJoinReport rep[1]; SPoly o;
        if (stroke_polyline_ex(c, 3, 2.0, 1, 0.1, 8, rep, &o) != 0) return 1;
        printf("拐角 %5.1f 度: 接法=%s 面积误差=%.6f"
               " (斜接 %.6f / 斜切 %.6f / 圆角 %.6f) 耗时=%.2f us\n",
               degs[i], stroke_join_name(rep[0].join), rep[0].err,
               rep[0].err_miter, rep[0].err_bevel, rep[0].err_round,
               rep[0].seconds * 1e6);
    }

    SPoint straight[3] = {{0,0},{10,0},{20,0}};
    SPoint reverse[3] = {{0,0},{10,0},{4,0}};
    SPoly o;
    printf("拐角   0 度: %s\n",
           stroke_polyline_ex(straight, 3, 2.0, 1, 0.1, 8, NULL, &o) == -3
           ? "拒绝（方向未改变，无角可接）" : "未拒绝!");
    printf("拐角 180 度: %s\n",
           stroke_polyline_ex(reverse, 3, 2.0, 1, 0.1, 8, NULL, &o) == -4
           ? "拒绝（线段折返，接法未定义）" : "未拒绝!");
    printf("误差相同: 固定选斜切（顶点最少、计算最简、结果确定）\n");

    SPoint q[4] = {{0,0},{30,0},{30,20},{0,20}};
    SPoly o1, o2;
    if (stroke_polyline(q, 4, 3.0, &o1) != 0) return 1;
    if (stroke_polyline_ex(q, 4, 3.0, 0, 0.0, 0, NULL, &o2) != 0) return 1;
    printf("关掉新逻辑: 输出%s\n",
           (o1.count == o2.count &&
            memcmp(o1.pts, o2.pts, (size_t)o1.count * sizeof(SPoint)) == 0)
           ? "逐字节不变" : "有差异!");
    return 0;
}
