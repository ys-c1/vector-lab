#include "src/scene.h"
#include "src/pipeline.h"
#include "src/fixed.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [--fixed [eps]]\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
    RScene s;
    scene_init(&s);
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
        if (scene_add_poly(&s, pts, n) != 0) { fclose(f); return 3; }
    }
    fclose(f);
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    int fixed = argc >= 7 && strcmp(argv[6], "--fixed") == 0;
    if (!fixed) {
        if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    } else {
        double eps = argc >= 8 ? atof(argv[7]) : 0.0;
        if (eps <= 0.0) { /* 默认目标精度：亚像素步长的 1/256 */
            double sx = (win.maxx - win.minx) / w, sy = (win.maxy - win.miny) / h;
            eps = (sx < sy ? sx : sy) / 4.0 / 256.0;
        }
        int q = fixed_bits_for_eps(eps);
        double worst; int qmax;
        if (fixed_check_overflow(&s, &win, q, &worst, &qmax) != 0) {
            fprintf(stderr, "overflow: worst=%g q=%d qmax=%d\n", worst, q, qmax);
            free(buf); return 8;
        }
        int rc = pipeline_render_fixed(&s, place, win, w, h, q, buf);
        if (rc == -4) { fprintf(stderr, "overflow during fixed transform\n"); free(buf); return 8; }
        if (rc != 0) { free(buf); return 6; }
        fprintf(stderr, "fixed q=%d eps=%g\n", q, eps);
    }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
