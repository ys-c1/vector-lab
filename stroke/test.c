#include "stroke.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    SPoint p[3] = {{0,0},{10,0},{10,10}};
    SPoly out;
    CK(stroke_polyline(p, 3, 2.0, &out) == 0);
    CK(out.count == 6);
    CK(out.pts[0].y > 0.9 && out.pts[0].y < 1.1);
    CK(stroke_polyline(p, 1, 2.0, &out) == -1);
    CK(stroke_polyline(p, 3, 0.0, &out) == -1);
    SPoint bad[2] = {{0,0},{0,0}};
    CK(stroke_polyline(bad, 2, 1.0, &out) == -2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
