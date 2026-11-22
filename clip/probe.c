/* 单窗裁剪逐字节探针：新旧构建跑完全相同的 clip_convex 调用，
 * 把结果顶点（裸 double）与面积以二进制 fwrite 落 stdout，
 * 供 cmp 逐字节比对。本文件只调用原有单窗 API。 */
#include "clip.h"
#include <stdio.h>

static void emit(const CPoly *p) {
    fwrite(&p->count, sizeof(int), 1, stdout);
    for (int i = 0; i < p->count; ++i) {
        fwrite(&p->pts[i].x, sizeof(double), 1, stdout);
        fwrite(&p->pts[i].y, sizeof(double), 1, stdout);
    }
    double a = clip_area(p);
    fwrite(&a, sizeof(double), 1, stdout);
}

int main(void) {
    struct { int n; CPoint src[8]; int wn; CPoint win[8]; } cases[] = {
        {4, {{0,0},{10,0},{10,10},{0,10}},
         4, {{2,2},{8,2},{8,8},{2,8}}},
        {4, {{0,0},{20,0},{20,10},{0,10}},
         4, {{5,-5},{15,-5},{15,5},{5,5}}},
        {5, {{0,0},{12,1},{13,9},{6,13},{-2,7}},
         4, {{3,2},{11,2},{11,10},{3,10}}},
        {4, {{-5,-5},{-1,-5},{-1,-1},{-5,-1}},
         4, {{2,2},{8,2},{8,8},{2,8}}},
        {4, {{0,0},{100,0},{100,100},{0,100}},
         4, {{30,30},{90,30},{90,90},{30,90}}},
    };
    int magic = 0xC11B;
    fwrite(&magic, sizeof(int), 1, stdout);
    int nc = (int)(sizeof(cases) / sizeof(cases[0]));
    fwrite(&nc, sizeof(int), 1, stdout);
    for (int i = 0; i < nc; ++i) {
        CPoly out;
        int rc = clip_convex(cases[i].src, cases[i].n,
                             cases[i].win, cases[i].wn, &out);
        fwrite(&rc, sizeof(int), 1, stdout);
        if (rc == 0) emit(&out);
    }
    return 0;
}
