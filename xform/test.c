#include "xform.h"
#include <math.h>
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

    /* Inverse transpose of a non-uniform scale (4, 1) sends (0,1) to
       (0,1); of (1, 0.25) it sends (0,1) to (0,4). */
    XForm ns = {4, 0, 0, 1, 0, 0};
    double ox = 0, oy = 0;
    CK(xform_inverse_transpose(ns, 0, 1, &ox, &oy) == 0);
    CK(fabs(ox) < 1e-12 && fabs(oy - 1.0) < 1e-12);
    XForm ns2 = {1, 0, 0, 0.25, 0, 0};
    CK(xform_inverse_transpose(ns2, 0, 1, &ox, &oy) == 0);
    CK(fabs(ox) < 1e-12 && fabs(oy - 4.0) < 1e-12);
    CK(fabs(xform_det(s) - 4.0) < 1e-12);

    /* Singular transform is reported and leaves outputs untouched. */
    XForm z = {0, 0, 0, 0, 0, 0};
    CK(xform_inverse_transpose(z, 0, 1, &ox, &oy) == -2);
    CK(xform_inverse_transpose(ns, 0, 1, NULL, &oy) == -1);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
