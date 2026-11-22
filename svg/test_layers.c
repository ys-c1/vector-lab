#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* 新逻辑测试：子路径切分与逐层灰阶；仅在 -DSVGPATH_MULTILAYER 下编译。*/
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    VPath p;
    CK(svg_parse("M5,5 L45,5 L45,45 L5,45 Z "
                 "M15,15 L35,15 L35,35 L15,35 Z "
                 "M21,21 L29,21 L29,29 L21,29 Z", &p) == 0);
    int w = 50, h = 50;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    CK(svg_fill_layers(&p, w, h, buf) == 0);

    /* (25,25) 在全部 3 层内 => 最深灰阶 153；(10,25) 只在第 1 层 => 255；
     * (48,25) 全在外 => 0。*/
    CK(buf[25 * w + 25] == 153);
    CK(buf[25 * w + 10] == 255);
    CK(buf[25 * w + 48] == 0);

    /* 退化为单轮廓时与旧填充的开/关像素集合一致（值为 255）。*/
    VPath q;
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &q) == 0);
    unsigned char *b2 = (unsigned char*)malloc(20 * 20);
    unsigned char *b3 = (unsigned char*)malloc(20 * 20);
    CK(svg_fill(&q, 20, 20, b2) == 0);
    CK(svg_fill_layers(&q, 20, 20, b3) == 0);
    int on2 = 0, on3 = 0;
    for (int i = 0; i < 400; ++i) {
        if (b2[i]) ++on2;
        if (b3[i]) ++on3;
    }
    CK(on2 == on3 && on2 > 40 && on2 < 60);
    free(buf); free(b2); free(b3);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
