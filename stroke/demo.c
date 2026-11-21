#include "stroke.h"
#include <stdio.h>
int main(void) {
    SPoint p[4] = {{0,0},{30,0},{30,20},{0,20}};
    SPoly out;
    if (stroke_polyline(p, 4, 3.0, &out) != 0) return 1;
    printf("outline %d\n", out.count);
    for (int i = 0; i < out.count; ++i) printf("  %.2f %.2f\n", out.pts[i].x, out.pts[i].y);
    return 0;
}
