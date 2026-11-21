#include "arclen.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    APoint p[3] = {{0,0},{10,0},{10,10}};
    ATable t;
    CK(arclen_build(p, 3, &t) == 0);
    CK(t.s[1] == 10.0 && t.s[2] == 20.0);
    CK(arclen_total(&t, p, 3) == 20.0);
    APoint m;
    CK(arclen_sample_at(p, 3, &t, 15.0, &m) == 0);
    CK(m.x > 9.9 && m.x < 10.1 && m.y > 4.9 && m.y < 5.1);
    CK(arclen_sample_at(p, 3, &t, -1.0, &m) == -2);
    CK(arclen_build(p, 1, &t) == -1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
