#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    VPath p;
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &p) == 0);
    CK(p.count == 3);
    unsigned char *buf = (unsigned char*)malloc(20 * 20);
    CK(svg_fill(&p, 20, 20, buf) == 0);
    int on = 0;
    for (int i = 0; i < 400; ++i) if (buf[i]) ++on;
    CK(on > 40 && on < 60);
    CK(svg_parse("L0,0", &p) == -2);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    CK(svg_parse("M0,0 L", &p) == -4);
    free(buf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
