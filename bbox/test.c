#include "bbox.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    GPoint p[3] = {{1,2},{5,0},{3,8}};
    GBox b;
    CK(bbox_of_points(p, 3, &b) == 0);
    CK(b.minx == 1 && b.maxx == 5 && b.miny == 0 && b.maxy == 8);
    CK(bbox_area(&b) == 32.0);
    GBox c = {0,0,2,2}, u;
    CK(bbox_union(&b, &c, &u) == 0);
    CK(u.minx == 0 && u.maxx == 5);
    CK(bbox_of_points(p, 0, &b) == -1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
