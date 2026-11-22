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

    /* 等分点：总长 100 的折线，按间距 25 取 5 个点 */
    APoint q[4] = {{0,0},{30,0},{30,40},{0,40}};
    APoint out[8];
    CK(arclen_even(q, 4, 25.0, NULL, 0) == 5);
    CK(arclen_even(q, 4, 25.0, out, 8) == 5);
    CK(out[0].x == 0.0 && out[0].y == 0.0);
    CK(out[1].x > 24.9 && out[1].x < 25.1 && out[1].y > -0.1 && out[1].y < 0.1);
    CK(out[2].x > 29.9 && out[2].x < 30.1 && out[2].y > 19.9 && out[2].y < 20.1);
    CK(out[3].x > 24.9 && out[3].x < 25.1 && out[3].y > 39.9 && out[3].y < 40.1);
    CK(out[4].x > -0.1 && out[4].x < 0.1 && out[4].y > 39.9 && out[4].y < 40.1);
    CK(arclen_even(q, 4, 50.0, out, 8) == 3);
    CK(arclen_even(q, 4, 25.0, out, 3) == ARCLEN_ERR_CAP);
    CK(arclen_even(q, 1, 10.0, out, 8) == ARCLEN_ERR_ARG);
    CK(arclen_even(q, 4, 0.0, out, 8) == ARCLEN_ERR_ARG);

    /* 三种拒绝：总长为零、段数小于二、间距比总长还大 */
    APoint z[3] = {{5,5},{5,5},{5,5}};
    CK(arclen_even(z, 3, 1.0, out, 8) == ARCLEN_ERR_TOTAL_ZERO);
    CK(arclen_even(q, 4, 60.0, out, 8) == ARCLEN_ERR_SEGCOUNT);
    CK(arclen_even(q, 4, 100.0, out, 8) == ARCLEN_ERR_SEGCOUNT);
    CK(arclen_even(q, 4, 101.0, out, 8) == ARCLEN_ERR_SPACING);
    CK(arclen_strerror(ARCLEN_ERR_TOTAL_ZERO) != 0);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
