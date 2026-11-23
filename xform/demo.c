#include "xform.h"
#include <locale.h>
#include <stdio.h>
int main(void) {
    setlocale(LC_ALL, "C");
    XForm s = {2, 0, 0, 2, 0, 0};
    XForm t = {1, 0, 0, 1, 5, 5};
    XForm m = xform_mul(t, s);
    XPoint p = {3, 4}, q = xform_apply(m, p);
    printf("mapped %.2f %.2f\n", q.x, q.y);
    return 0;
}
