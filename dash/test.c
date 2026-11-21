#include "dash.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    DPoint p[2] = {{0,0},{100,0}};
    DPoly out;
    CK(dash_polyline(p, 2, 10.0, 10.0, &out) == 0);
    CK(out.count > 80);
    CK(out.pts[0].x >= 0.0 && out.pts[0].x < 1.0);
    CK(dash_polyline(p, 2, 0.0, 10.0, &out) == -1);
    DPoint bad[2] = {{0,0},{0,0}};
    CK(dash_polyline(bad, 2, 5.0, 5.0, &out) == -2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
