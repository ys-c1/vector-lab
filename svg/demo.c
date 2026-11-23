#include "svgpath.h"
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
int main(void) {
    setlocale(LC_ALL, "C");
    VPath p;
    if (svg_parse("M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z", &p) != 0) return 1;
    int w = 50, h = 50;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (svg_fill(&p, w, h, buf) != 0) return 1;
    int on = 0;
    for (int i = 0; i < w * h; ++i) if (buf[i]) ++on;
    if (svg_save_pgm("demo.pgm", buf, w, h) != 0) return 1;
    printf("segs %d on %d\n", p.count, on);
    free(buf);
    return 0;
}
