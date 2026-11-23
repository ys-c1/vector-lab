#include "arclen.h"
#include <locale.h>
#include <stdio.h>
int main(void) {
    setlocale(LC_ALL, "C");
    APoint p[4] = {{0,0},{30,0},{30,20},{0,20}};
    ATable t;
    if (arclen_build(p, 4, &t) != 0) return 1;
    printf("total %.2f\n", arclen_total(&t, p, 4));
    for (int i = 0; i < 4; ++i) printf("  s[%d]=%.2f\n", i, t.s[i]);
    return 0;
}
