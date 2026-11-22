#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/fit.h"
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
        fprintf(stderr,
                "usage: %s scene.txt out.pgm w h scale [budget]\n"
                "  fixed mode (no budget): render at w x h exactly\n"
                "  budget mode (budget>0): largest square side <= min(w,h)\n"
                "                          whose PGM bytes <= budget\n",
                argv[0]);
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
    RPlace place = {0.0, 0.0, 0.0, sc};

    /* 固定尺寸模式：原逻辑保持不变。 */
    if (argc < 7) {
        unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
        if (!buf) return 5;
        if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
        if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
        printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
        free(buf);
        return 0;
    }

    /* 字节预算模式：反解最大方形边长。 */
    long long budget = atoll(argv[6]);
    int cap = w < h ? w : h;
    RFitResult r;
    double t0 = now_sec();
    int st = fit_solve(&s, place, win, cap, budget, &r);
    double dt = now_sec() - t0;
    printf("fit status=%s budget=%lld cap=%d size=%d bytes=%lld on=%d ref_on=%d "
           "recognizable=%d min_size=%d header_min=%d elapsed_sec=%.4f\n",
           fit_status_str(st), r.budget, r.cap, r.size, r.bytes, r.on, r.ref_on,
           r.recognizable, r.min_size, r.header_min, dt);
    if (st != FIT_OK) {
        fprintf(stderr, "fit rejected: %s\n", fit_status_str(st));
        return 10 + st;
    }

    int side = r.size;
    unsigned char *buf = (unsigned char*)malloc((size_t)side * side);
    if (!buf) return 5;
    if (pipeline_render(&s, place, win, side, side, buf) != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, side, side) != 0) { free(buf); return 7; }
    long long actual = export_pgm_bytes(side, side);
    printf("wrote %s side=%d bytes=%lld within_budget=%d\n",
           argv[2], side, actual, actual <= budget);
    free(buf);
    return 0;
}
