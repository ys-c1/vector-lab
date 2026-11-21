#include "dash.h"
#include <stdio.h>
int main(void) {
    DPoint p[3] = {{0,0},{60,0},{60,40}};
    DPoly out;
    if (dash_polyline(p, 3, 8.0, 6.0, &out) != 0) return 1;
    printf("dash points %d\n", out.count);
    return 0;
}
