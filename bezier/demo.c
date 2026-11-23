#include "bezier.h"
#include <locale.h>
#include <stdio.h>
int main(void) {
    setlocale(LC_ALL, "C");
    BPoint c[4] = {{0,0},{20,40},{60,40},{80,0}};
    BPoly p;
    if (bezier_flatten(c, 0.5, &p) != 0) return 1;
    printf("points %d chord %.2f\n", p.count, bezier_chord(c));
    for (int i = 0; i < p.count; ++i) printf("  %.2f %.2f\n", p.pts[i].x, p.pts[i].y);
    return 0;
}
