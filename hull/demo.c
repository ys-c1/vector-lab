#include "hull.h"
#include <locale.h>
#include <stdio.h>
int main(void) {
    setlocale(LC_ALL, "C");
    HPoint p[6] = {{0,0},{5,0},{5,4},{2,1},{0,4},{3,2}};
    HPoly out;
    if (hull_convex(p, 6, &out) != 0) return 1;
    printf("hull %d\n", out.count);
    for (int i = 0; i < out.count; ++i) printf("  %.2f %.2f\n", out.pts[i].x, out.pts[i].y);
    return 0;
}
