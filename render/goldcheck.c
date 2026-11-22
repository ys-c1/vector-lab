#define _POSIX_C_SOURCE 200809L
/* 交叉校验：新覆盖率渲染器 vs gold/ref 的 8x8 超采样灰阶。
 * gold/ref 是独立实现（不同代码、不同遍历顺序），只做参照。
 * 两者都按 255*c/SS² 出灰，但取整方式不同（gold 截断、我们四舍五入），
 * 因此允许每像素差 1；若差 >1 说明覆盖率算法本身不一致。
 *
 * 用法：goldcheck <scene.txt> <ref 二进制> [ss]
 * 退出码 0 = 一致。 */
#include "src/scene.h"
#include "src/raster.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    scene_init(s);
    char tag[16];
    while (fscanf(f, "%15s", tag) == 1) {
        if (tag[0] != 'p') break;
        RPoint pts[64];
        int n = 0;
        while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) {
            ++n;
            int c = fgetc(f);
            if (c == '\n' || c == EOF) break;
            ungetc(c, f);
        }
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -2; }
    }
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s scene.txt ref-binary [ss] [w h]\n", argv[0]);
        return 1;
    }
    int ss = argc > 3 ? atoi(argv[3]) : 8;
    RScene s;
    if (load_scene(argv[1], &s) != 0 || s.npolys != 1) {
        fprintf(stderr, "scene must contain exactly one polygon\n");
        return 2;
    }
    RBox b;
    if (scene_bbox(&s, &b) != 0) return 2;
    int w = argc > 5 ? atoi(argv[4]) : 61;
    int h = argc > 5 ? atoi(argv[5]) : 61;

    /* gold 要每行 "x y" */
    char polyf[] = "/tmp/goldcheck_polyXXXXXX";
    int fd = mkstemp(polyf);
    if (fd < 0) return 3;
    FILE *pf = fdopen(fd, "w");
    for (int i = 0; i < s.polys[0].n; ++i)
        fprintf(pf, "%.10f %.10f\n", s.polys[0].pts[i].x, s.polys[0].pts[i].y);
    fclose(pf);

    char goldf[] = "/tmp/goldcheck_goldXXXXXX";
    fd = mkstemp(goldf);
    if (fd < 0) { unlink(polyf); return 3; }
    close(fd);
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s pixels %s %d %d > %s", argv[2], polyf, w, h, goldf);
    if (system(cmd) != 0) { unlink(polyf); unlink(goldf); return 4; }

    FILE *gf = fopen(goldf, "rb");
    if (!gf) { unlink(polyf); unlink(goldf); return 4; }
    int gw, gh, gmax;
    if (fscanf(gf, "P5 %d %d %d", &gw, &gh, &gmax) != 3 || gw != w || gh != h) {
        fclose(gf); unlink(polyf); unlink(goldf); return 5;
    }
    fgetc(gf); /* 单个空白 */
    unsigned char *gold = (unsigned char *)malloc((size_t)w * h);
    if (fread(gold, 1, (size_t)w * h, gf) != (size_t)w * h) {
        fclose(gf); free(gold); unlink(polyf); unlink(goldf); return 5;
    }
    fclose(gf);

    unsigned char *mine = (unsigned char *)malloc((size_t)w * h);
    if (raster_coverage(&s, &b, w, h, ss, mine) != 0) {
        free(gold); free(mine); unlink(polyf); unlink(goldf); return 6;
    }
    int maxd = 0, nz = 0;
    for (int i = 0; i < w * h; ++i) {
        int d = abs((int)mine[i] - (int)gold[i]);
        if (d > maxd) maxd = d;
        if (d) ++nz;
    }
    free(gold); free(mine); unlink(polyf); unlink(goldf);
    printf("goldcheck %s %dx%d ss=%d maxdiff=%d nonzero=%d %s\n",
           argv[1], w, h, ss, maxd, nz, maxd <= 1 ? "OK" : "MISMATCH");
    return maxd <= 1 ? 0 : 7;
}
