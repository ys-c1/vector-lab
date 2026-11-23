#include "clip.h"
#include <locale.h>
#include <stdio.h>
int main(void) {
    setlocale(LC_ALL, "C");
    CPoint poly[4] = {{0,0},{20,0},{20,10},{0,10}};
    CPoint win[4] = {{5,-5},{15,-5},{15,5},{5,5}};
    CPoly out;
    if (clip_convex(poly,4,win,4,&out) != 0) return 1;
    printf("clipped %d area %.2f\n", out.count, clip_area(&out));
    return 0;
}
