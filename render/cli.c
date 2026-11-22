#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/aspect.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [fixed|aspect]\n", argv[0]);
        return 1;
    }
    const char *mode = (argc >= 7) ? argv[6] : "fixed";
    int aspect_on = (mode[0] == 'a');
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

    if (aspect_on) {
        /* 先求解、再分配缓冲：零/负像素尺寸也要能指出是哪条边 */
        ASolve sol;
        double t0 = now_sec();
        int rc = aspect_solve(&win, w, h, &sol);
        double t_solve = now_sec() - t0;
        if (rc != 0) {
            if (sol.degenerate == ASPECT_DEGEN_POINT)
                fprintf(stderr,
                    "reject: degenerate %s (all four edges collapse to one point); "
                    "zero edge pair: %s\n",
                    aspect_degen_name(sol.degenerate), aspect_edge_name(sol.bad_edge));
            else
                fprintf(stderr,
                    "reject: non-positive ratio, bad edge pair: %s%s\n",
                    aspect_edge_name(sol.bad_edge),
                    sol.degenerate ? " (degenerate line)" : " (pixel size)");
            return 8;
        }
        unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
        if (!buf) return 5;
        RPlace place = {0.0, 0.0, 0.0, sc};
        double t1 = now_sec();
        if (pipeline_render(&s, place, sol.win, w, h, buf) != 0) { free(buf); return 6; }
        if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
        double t_render = now_sec() - t1;
        double rx, ry, dev;
        if (aspect_roundness(buf, w, h, &rx, &ry, &dev) != 0) { free(buf); return 9; }
        printf("aspect: win_ratio=%.6f px_ratio=%.6f win=[L%.4f R%.4f B%.4f T%.4f] "
               "rx=%.3f ry=%.3f round_dev=%.4f%% %s solve=%.0fns total=%.3fms\n",
               sol.ratio, sol.px_ratio,
               sol.win.minx, sol.win.maxx, sol.win.miny, sol.win.maxy,
               rx, ry, dev * 100.0,
               (dev <= 0.005) ? "PASS(<=0.5%)" : "FAIL(>0.5%)",
               t_solve * 1e9, (t_solve + t_render) * 1e3);
        printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s),
               export_count_on(buf, w, h));
        free(buf);
        return 0;
    }

    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
