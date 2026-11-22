#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
/* 新逻辑演示：三层嵌套路径，每层用不同灰阶渲染到 demo-layers.pgm。*/
int main(void) {
    VPath p;
    if (svg_parse("M5,5 L45,5 L45,45 L5,45 Z "
                  "M15,15 L35,15 L35,35 L15,35 Z "
                  "M21,21 L29,21 L29,29 L21,29 Z", &p) != 0) return 1;
    int w = 50, h = 50;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (svg_fill_layers(&p, w, h, buf) != 0) return 1;
    int hist[256] = {0};
    for (int i = 0; i < w * h; ++i) ++hist[buf[i]];
    if (svg_save_pgm("demo-layers.pgm", buf, w, h) != 0) return 1;
    printf("segs %d layers-pixels bg=%d L1=%d L2=%d L3=%d\n",
           p.count, hist[0], hist[255], hist[204], hist[153]);
    free(buf);
    return 0;
}
