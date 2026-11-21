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
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
