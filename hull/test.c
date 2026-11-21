#include "hull.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    HPoint p[5] = {{0,0},{4,0},{4,3},{0,3},{2,1}};
    HPoly out;
    CK(hull_convex(p, 5, &out) == 0);
    CK(out.count == 4);
    double cx, cy, w, h, a;
    CK(hull_min_rect(p, 5, &cx, &cy, &w, &h, &a) == 0);
    CK(w > 3.9 && w < 4.1 && h > 2.9 && h < 3.1);
    CK(hull_convex(p, 0, &out) == -1);
    CK(hull_convex(p, 1, &out) == 0 && out.count == 1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
