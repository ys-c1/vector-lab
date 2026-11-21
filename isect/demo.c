#include "isect.h"
#include <stdio.h>
int main(void) {
    IPoint a[3] = {{0,0},{10,0},{10,10}};
    IPoint b[2] = {{0,5},{12,5}};
    int hits = 0;
    if (isect_polyline(a,3,b,2,&hits) != 0) return 1;
    printf("hits %d\n", hits);
    return 0;
}
