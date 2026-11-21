#include "bezier.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    BPoint c[4] = {{0,0},{0,10},{10,10},{10,0}};
    BPoint m = bezier_eval(c, 0.5);
    CK(m.x > 4.9 && m.x < 5.1);
    CK(m.y > 7.4 && m.y < 7.6);
    BPoly p;
    CK(bezier_flatten(c, 0.25, &p) == 0);
    CK(p.count == 9);
    CK(bezier_chord(c) > 9.9 && bezier_chord(c) < 10.1);
    CK(bezier_flatten(c, 0.0, &p) == -1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
