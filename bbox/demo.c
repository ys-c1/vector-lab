#include "bbox.h"
#include <stdio.h>
int main(void) {
    GPoint p[5] = {{0,0},{10,4},{4,10},{-2,3},{6,-1}};
    GBox b;
    if (bbox_of_points(p, 5, &b) != 0) return 1;
    printf("box %.2f %.2f %.2f %.2f area %.2f\n", b.minx, b.miny, b.maxx, b.maxy, bbox_area(&b));
    return 0;
}
