#include "xform.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    XForm id = xform_identity();
    XPoint p = {2, 3};
    XPoint q = xform_apply(id, p);
    CK(q.x == 2 && q.y == 3);
    XForm t = {1, 0, 0, 1, 10, 20};
    XPoint r = xform_apply(t, p);
    CK(r.x == 12 && r.y == 23);
    XForm s = {2, 0, 0, 2, 0, 0};
    XForm m = xform_mul(s, t);
    XPoint u = xform_apply(m, p);
    CK(u.x == 24 && u.y == 46);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
