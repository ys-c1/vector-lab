#include "src/scene.h"
#include "src/pipeline.h"
#include "src/parallel.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>

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
    return s->npolys > 0 ? 0 : -3;
}

static void usage(const char *a0) {
    fprintf(stderr, "usage: %s <scene.txt> <out.pgm> <w> <h> <scale> <threads> <block>\n", a0);
}

int main(int argc, char **argv) {
    if (argc < 8) { usage(argv[0]); return 1; }
    RScene s;
    if (load_scene(argv[1], &s) != 0) { fprintf(stderr, "load scene failed\n"); return 2; }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
    RParallelCfg cfg;
    cfg.threads = atoi(argv[6]);
    cfg.block = atoi(argv[7]);
    if (w <= 0 || h <= 0) {
        fprintf(stderr, "failed: 宽高 %d x %d 非法，已停止\n", w, h);
        return 6;
    }
    RBox win;
    if (scene_bbox(&s, &win) != 0) {
        fprintf(stderr, "failed: 空场景，已停止\n");
        return 6;
    }
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) { fprintf(stderr, "failed: 缓冲区分配失败，已停止\n"); return 5; }

    RPlace place = {0.0, 0.0, 0.0, sc};
    char err[256];
    int rc = pipeline_render_parallel(&s, place, win, w, h, &cfg, buf, err, sizeof err);
    if (rc != RP_OK) {
        fprintf(stderr, "failed: %s\n", err[0] ? err : rp_strerror(rc));
        free(buf);
        return 6;
    }
    int off = export_pgm(argv[2], buf, w, h);
    int on = export_count_on(buf, w, h);
    free(buf);
    if (off != 0) { fprintf(stderr, "failed: 写出 %s 失败\n", argv[2]); return 7; }
    printf("polys %d verts %d on %d threads %d block %d\n",
           s.npolys, scene_count_verts(&s), on, cfg.threads, cfg.block);
    return 0;
}
