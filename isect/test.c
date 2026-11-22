#include "isect.h"
#include <math.h>
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    IPoint a0 = {0,0}, a1 = {10,0}, b0 = {5,-5}, b1 = {5,5};
    IHit h;
    CK(isect_seg(a0,a1,b0,b1,&h) == 1);
    CK(h.at.x > 4.9 && h.at.x < 5.1);
    IPoint c0 = {0,1}, c1 = {10,1};
    CK(isect_seg(a0,a1,c0,c1,&h) == 0);
    IPoint pa[2] = {{0,0},{10,0}}, pb[2] = {{5,-5},{5,5}};
    int hits = 0;
    CK(isect_polyline(pa,2,pb,2,&hits) == 0 && hits == 1);
    IPoint bow[5] = {{0,0},{10,10},{10,0},{0,10},{0,0}};
    int ia = -1, ib = -1;
    CK(isect_self(bow,5,&ia,&ib) == 1);
    CK(isect_polyline(pa,1,pb,2,&hits) == -1);

    /* ---- gap 谓词：距离语义，不含固定阈值 ---- */
    CK(isect_gap(a0, a1, b0, b1) == 0.0);                 /* 交叉 -> 0 */
    CK(fabs(isect_gap(a0, a1, c0, c1) - 1.0) < 1e-12);    /* 平行间距 1 */
    IPoint d0 = {20, 0}, d1 = {30, 3};
    CK(fabs(isect_gap(a0, a1, d0, d1) - 10.0) < 1e-12);   /* 隔离段端点垂距 (10,0)->(20,0) */
    IPoint d2 = {20, 3}, d3 = {30, 3};
    CK(fabs(isect_gap(a0, a1, d2, d3) - sqrt(109.0)) < 1e-11);
    IPoint e0 = {5, 0}, e1 = {5, 0};
    CK(isect_gap(a0, a1, e0, e1) == 0.0);                 /* 零长点落在线上 */
    CK(isect_zero_len(e0, e1) == 1);
    CK(isect_zero_len(a0, a1) == 0);
    IPoint f0 = {5, 2}, f1 = {5, 2};
    CK(fabs(isect_gap(a0, a1, f0, f1) - 2.0) < 1e-12);    /* 点到线距离 */
    IPoint g0 = {0, 0}, g1 = {0, 0}, g2 = {3, 4}, g3 = {3, 4};
    CK(fabs(isect_gap(g0, g1, g2, g3) - 5.0) < 1e-12);    /* 两点距离 */
    IPoint h0 = {0, 0}, h1 = {10, 0}, h2 = {2, 0}, h3 = {6, 0};
    CK(isect_gap(h0, h1, h2, h3) == 0.0);                 /* 共线重叠 */
    IPoint k0 = {20, 0}, k1 = {30, 0};
    CK(fabs(isect_gap(h0, h1, k0, k1) - 10.0) < 1e-12);   /* 共线不重叠 */
    IPoint t0 = {10, 0}, t1 = {10, 0}, t2 = {10, 0}, t3 = {10, 5};
    CK(isect_gap(t0, t1, t2, t3) == 0.0);                 /* 点在端点 */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
