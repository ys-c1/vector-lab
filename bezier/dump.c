/*
 * 几何导出：把自适应细分后的折线写成 gold/ref.c 的 "x y" 多边形格式，
 * 供 gold 参考实现栅格化/求面积；同时导出旧固定 8 段的点序列，
 * 用于“关掉新逻辑时逐字节不变”的比对。
 *
 * 用法：./build/dump <curve> <err_bound> <out.txt>
 *   curve ∈ gentle|sharp|S|line|legacy
 *   legacy 使用旧固定 8 段（忽略 err_bound），与 bezier/demo.c 的曲线相同。
 */
#include "bezier.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const BPoint LEGACY_CURVE[4] = {{0,0},{20,40},{60,40},{80,0}};

static int curve_by_name(const char *name, BPoint c[4], double *err) {
    if (!strcmp(name, "gentle")) {
        BPoint p[4] = {{0,0},{25,8},{75,8},{100,0}};
        memcpy(c, p, sizeof p);
        *err = 0.50;
        return 0;
    }
    if (!strcmp(name, "sharp")) {
        BPoint p[4] = {{0,0},{100,0},{100,100},{0,100}};
        memcpy(c, p, sizeof p);
        *err = 0.05;
        return 0;
    }
    if (!strcmp(name, "S")) {
        BPoint p[4] = {{0,0},{100,50},{0,-50},{100,0}};
        memcpy(c, p, sizeof p);
        *err = 0.10;
        return 0;
    }
    if (!strcmp(name, "line")) {
        BPoint p[4] = {{0,0},{33,0.2},{66,-0.2},{100,0}};
        memcpy(c, p, sizeof p);
        *err = 0.02;
        return 0;
    }
    if (!strcmp(name, "legacy")) {
        memcpy(c, LEGACY_CURVE, sizeof LEGACY_CURVE);
        *err = 0.5;
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <gentle|sharp|S|line|legacy> <err> <out.txt>\n",
                argv[0]);
        return 1;
    }
    BPoint c[4];
    double default_err = 0.0;
    if (curve_by_name(argv[1], c, &default_err) != 0) {
        fprintf(stderr, "unknown curve '%s'\n", argv[1]);
        return 2;
    }
    char *end = NULL;
    double err = strtod(argv[2], &end);
    if (end == argv[2] || err <= 0.0) err = default_err;

    BFlat f;
    BPathResult r;
    int rc;
    if (!strcmp(argv[1], "legacy")) {
        rc = bezier_flatten_mode(c, 0.5, BEZ_MODE_LEGACY_FIXED, &f, &r);
    } else {
        rc = bezier_flatten_path((const BPoint (*)[4]) &c, 1, err, &f, &r);
    }
    if (rc != BEZ_OK) {
        fprintf(stderr, "flatten failed rc=%d (%s)", rc, bezier_strerror(rc));
        if (r.bad_seg >= 0) fprintf(stderr, " segment %d", r.bad_seg);
        fprintf(stderr, "\n");
        return 3;
    }
    FILE *fp = fopen(argv[3], "w");
    if (!fp) { perror(argv[3]); return 4; }
    for (int i = 0; i <= f.pieces; ++i)
        fprintf(fp, "%.17g %.17g\n", f.pts[i].x, f.pts[i].y);
    fclose(fp);
    printf("%s: %d pieces, T=%.6f -> %s\n", argv[1], f.pieces,
           r.tol > 0.0 ? r.tol : bezier_tol_for_error(err), argv[3]);
    return 0;
}
