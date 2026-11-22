/* 旧逻辑的逐字节副本。
 *
 * 这里是"关掉新逻辑"的基线：把输入窗口顶点原样交给奇偶规则数格子，
 * 与 render/src/raster.c 的 inside()/raster_fill() 与 export.c 的 PGM 头
 * 逐表达式对齐。基线 PGM 由本文件生成，主驱动的 mode=0 结果必须与它
 * 逐字节相同；改动本文件即等于改了基线，不允许。
 *
 * 编译：gcc -O2 -I../render/src baseline/legacy.c ../render/src/raster.c \
 *            ../render/src/export.c ../render/src/scene.c
 */
#include "scene.h"
#include "raster.h"
#include "export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 把窗口文件里的每个窗口按输入顺序装成场景（不拆分、不归一化）。 */
static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    scene_init(s);
    RPoint pts[64];
    int n = 0;
    char line[4096];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == '#' || *p == '\n' || *p == '\0') continue;
        if (*p == 'W') {
            if (n >= 3 && scene_add_poly(s, pts, n) != 0) { fclose(f); return -2; }
            n = 0;
            continue;
        }
        if (*p == 'S') continue;   /* 拆分声明对老逻辑无意义：整环照画 */
        double x, y;
        char extra;
        if (sscanf(p, "%lf %lf %c", &x, &y, &extra) != 2) { fclose(f); return -3; }
        if (n >= 64) { fclose(f); return -4; }
        pts[n].x = x;
        pts[n].y = y;
        ++n;
    }
    if (n >= 3 && scene_add_poly(s, pts, n) != 0) { fclose(f); return -5; }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s windows.txt out.pgm w h ss\n", argv[0]);
        return 1;
    }
    RScene s;
    if (load_scene(argv[1], &s) != 0) { fprintf(stderr, "load failed\n"); return 2; }
    int w = atoi(argv[3]), h = atoi(argv[4]), ss = atoi(argv[5]);
    RBox b;
    if (scene_bbox(&s, &b) != 0) return 3;
    unsigned char *buf = (unsigned char *)malloc((size_t)w * h);
    if (!buf) return 4;
    if (raster_fill(&s, &b, w, h, ss, buf) != 0) { free(buf); return 5; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 6; }
    printf("legacy: polys %d verts %d box [%.3f %.3f %.3f %.3f]\n",
           s.npolys, scene_count_verts(&s), b.minx, b.miny, b.maxx, b.maxy);
    free(buf);
    return 0;
}
