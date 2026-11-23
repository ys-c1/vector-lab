#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/meta.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(const char *prog) {
    fprintf(stderr,
        "usage: %s [--no-meta] [--seq-file f] [--meta-file f] scene.txt out.pgm w h scale\n",
        prog);
}

int main(int argc, char **argv) {
    int meta_on = 1;
    const char *seq_path = "render.seq";
    const char *meta_path = NULL;   /* 默认 out.pgm 去后缀 + .meta.json */
    int ai = 1;
    while (ai < argc && argv[ai][0] == '-' && argv[ai][1] == '-') {
        if (!strcmp(argv[ai], "--no-meta")) {
            meta_on = 0;
            ++ai;
        } else if (!strcmp(argv[ai], "--seq-file") && ai + 1 < argc) {
            seq_path = argv[ai + 1];
            ai += 2;
        } else if (!strcmp(argv[ai], "--meta-file") && ai + 1 < argc) {
            meta_path = argv[ai + 1];
            ai += 2;
        } else {
            usage(argv[0]);
            return 1;
        }
    }
    if (argc - ai < 5) { usage(argv[0]); return 1; }
    const char *scene_path = argv[ai];
    const char *out_path = argv[ai + 1];
    int w = atoi(argv[ai + 2]), h = atoi(argv[ai + 3]);
    double sc = atof(argv[ai + 4]);

    char meta_default[512];
    if (meta_on && !meta_path) {
        int n = snprintf(meta_default, sizeof meta_default, "%s", out_path);
        if (n <= 0 || (size_t)n >= sizeof meta_default) return 8;
        char *dot = strrchr(meta_default, '.');
        if (dot) *dot = '\0';
        n = snprintf(meta_default + strlen(meta_default),
                     sizeof meta_default - strlen(meta_default), ".meta.json");
        if (n <= 0 || (size_t)n >= sizeof meta_default) return 8;
        meta_path = meta_default;
    }

    FILE *f = fopen(scene_path, "r");
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
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};

    long long t0 = meta_on ? meta_now_ms() : 0;
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(out_path, buf, w, h) != 0) { free(buf); return 7; }
    long long t1 = meta_on ? meta_now_ms() : 0;

    if (meta_on) {
        if (t0 < 0 || t1 < 0 || t1 < t0) { free(buf); return 9; }
        long long last = 0, peak = 0, seq = 0;
        int rc = meta_seq_load(seq_path, &last, &peak);
        if (rc != 0) { fprintf(stderr, "seq load rejected: %d\n", rc); free(buf); return 10; }
        rc = meta_seq_next(seq_path, last, peak, &seq);
        if (rc != 0) { fprintf(stderr, "seq advance rejected: %d\n", rc); free(buf); return 11; }
        RMeta m = { w, h, scene_count_verts(&s), seq, t1 - t0 };
        rc = meta_write(meta_path, &m);
        if (rc != 0) { fprintf(stderr, "meta write rejected: %d\n", rc); free(buf); return 12; }
    }

    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
