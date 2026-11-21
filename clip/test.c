#include "clip.h"
#include <stdio.h>
#include <math.h>
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
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
