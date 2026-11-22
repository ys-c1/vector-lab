#include "simplify.h"
#include <math.h>
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* 1. 正方形 + 每条边 2 个共线点 + 1 个重合点 */
    HPoint sq[13] = {
        {0,0},{10,0},{10,10},{0,10},
        {2.5,0},{7.5,0},{10,2.5},{10,7.5},
        {7.5,10},{2.5,10},{0,7.5},{0,2.5},
        {0,0}
    };
    HSimp r;
    CK(hull_simplify(sq, 13, 0.95, &r) == 0);
    CK(r.collinear == 9);                 /* 8 边内点 + 1 重合点 */
    CK(r.hull_count == 4 && r.kept == 4); /* 正方形无可剔顶点 */
    CK(fabs(r.ratio - 1.0) < 1e-12);
    CK(fabs(r.area_full - 100.0) < 1e-9);

    /* 2. 比例 1.0：一个顶点都不能剔 */
    CK(hull_simplify(sq, 13, 1.0, &r) == 0);
    CK(r.kept == 4 && r.ratio >= 1.0 - 1e-12);

    /* 3. 非法比例与参数 */
    CK(hull_simplify(sq, 13, 0.0, &r) == -2);
    CK(hull_simplify(sq, 13, 1.5, &r) == -2);
    CK(hull_simplify(sq, 13, -0.1, &r) == -2);
    CK(hull_simplify(NULL, 13, 0.9, &r) == -1);
    CK(hull_simplify(sq, 2, 0.9, &r) == -3);

    /* 4. 全共线：严格凸包 < 3，拒并 */
    HPoint flat[5] = {{0,0},{1,1},{2,2},{3,3},{4,4}};
    CK(hull_simplify(flat, 5, 0.9, &r) == -4);

    /* 5. 三点点集本身就是三角形：不可再剔 */
    HPoint tri[3] = {{0,0},{4,0},{0,3}};
    CK(hull_simplify(tri, 3, 0.5, &r) == 0);
    CK(r.kept == 3);
    CK(fabs(r.area_full - 6.0) < 1e-12);

    /* 6. 正八边形：剔除后面积比不得低于目标，且估计在合理区间 */
    HPoint oct[8];
    for (int i = 0; i < 8; ++i) {
        double a = 2.0 * 3.14159265358979 * i / 8.0;
        oct[i].x = 5.0 * cos(a); oct[i].y = 5.0 * sin(a);
    }
    CK(hull_simplify(oct, 8, 0.80, &r) == 0);
    CK(r.kept >= 3 && r.kept <= 8);
    CK(r.ratio + 1e-12 >= 0.80);
    CK(r.est_count >= 3 && r.est_count <= 8);

    /* 7. 解析估计单调性：目标比例越高，估计点数越多 */
    int k80 = hull_estimate_keep(64, 0.80);
    int k95 = hull_estimate_keep(64, 0.95);
    int k99 = hull_estimate_keep(64, 0.99);
    CK(k80 < k95 && k95 < k99);
    CK(hull_estimate_keep(3, 0.5) == 3);
    CK(hull_estimate_keep(64, 1.0) == 64);

    /* 8. 估计的精度：n = pi*sqrt(2/(3(1-r)))
     *    r=0.80 -> 5.74 -> 6；r=0.95 -> 11.47 -> 12；r=0.99 -> 25.65 -> 26 */
    CK(k80 == 6);
    CK(k95 == 12);
    CK(k99 == 26);

    /* 9. 64 点满容量正多边形：回归单调链 n+1 槽越界（曾写穿 HPoly.pts） */
    HPoint big[64];
    for (int i = 0; i < 64; ++i) {
        double a = 2.0 * 3.14159265358979 * i / 64.0;
        big[i].x = 7.0 * cos(a); big[i].y = 7.0 * sin(a);
    }
    HPoly hb;
    CK(hull_convex(big, 64, &hb) == 0 && hb.count == 64);
    CK(hull_simplify(big, 64, 0.99, &r) == 0);
    CK(r.kept == k99 && r.ratio + 1e-12 >= 0.99);

    /* 10. hull_convex 对全共线输入返回 2 个端点且不越界，simplify 据此拒并 */
    HPoint line6[6] = {{0,0},{1,1},{2,2},{3,3},{4,4},{5,5}};
    HPoly hl;
    CK(hull_convex(line6, 6, &hl) == 0 && hl.count == 2);
    CK(hull_simplify(line6, 6, 0.9, &r) == -4);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
