#include "isect.h"
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

    /* 容差扩展：端点外 1.5px 的接触 */
    IPoint e0 = {0,0}, e1 = {10,0}, f0 = {11.5,-5}, f1 = {11.5,5};
    CK(isect_seg(e0,e1,f0,f1,&h) == 0);              /* 现状：直接漏掉 */
    CK(isect_seg_tol(e0,e1,f0,f1,0.0,&h) == 0);      /* 不扩容差，与现状一致 */
    CK(isect_seg_tol(e0,e1,f0,f1,1.0,&h) == 0);      /* 1.0 容差 < 1.5 间隙 */
    CK(isect_seg_tol(e0,e1,f0,f1,2.0,&h) == 1);      /* 2.0 容差补上 */
    CK(h.at.x > 11.4 && h.at.x < 11.6);
    CK(h.t > 1.0);                                   /* 换算回原段参数，落在端点外 */
    /* 容差为负、段长为零要报错 */
    CK(isect_seg_tol(e0,e1,f0,f1,-0.5,&h) == -1);
    IPoint z0 = {3,3}, z1 = {3,3};
    CK(isect_seg_tol(z0,z1,f0,f1,1.0,&h) == -2);
    CK(isect_seg_tol(e0,e1,z0,z1,1.0,&h) == -3);
    /* 折线版：tol=0 与旧接口一致；报错指出段号 */
    IPoint qa[3] = {{0,0},{10,0},{20,0}};
    IPoint qb[2] = {{21.5,-5},{21.5,5}};
    int eseg = -9;
    CK(isect_polyline_tol(qa,3,qb,2,0.0,&hits,&eseg) == 0 && hits == 0);
    CK(isect_polyline_tol(qa,3,qb,2,2.0,&hits,&eseg) == 0 && hits == 1);
    CK(isect_polyline_tol(qa,3,qb,2,-1.0,&hits,&eseg) == -2);
    IPoint qz[4] = {{0,0},{10,0},{10,0},{20,0}};     /* 段 1 长度为零 */
    CK(isect_polyline_tol(qz,4,qb,2,1.0,&hits,&eseg) == -3 && eseg == 1);
    IPoint qzb[3] = {{5,5},{5,5},{9,9}};             /* b 的段 0 长度为零 */
    CK(isect_polyline_tol(qa,3,qzb,3,1.0,&hits,&eseg) == -4 && eseg == 0);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
