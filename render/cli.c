#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/jitter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
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
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return 3; }
    }
    fclose(f);
    return 0;
}

/* legacy: cli scene.txt out.pgm w h scale  (equal-partition, unchanged) */
static int cmd_render(char **argv) {
    RScene s;
    scene_init(&s);
    int rc = load_scene(argv[1], &s);
    if (rc != 0) return rc;
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}

/* cli jitter scene.txt out.pgm w h scale ss seed */
static int cmd_jitter(char **argv) {
    RScene s;
    scene_init(&s);
    int rc = load_scene(argv[2], &s);
    if (rc != 0) return rc;
    int w = atoi(argv[4]), h = atoi(argv[5]);
    double sc = atof(argv[6]);
    int ss = atoi(argv[7]);
    unsigned long long seed = strtoull(argv[8], NULL, 10);
    if (seed == 0) { fprintf(stderr, "rejected: seed must be nonzero (zero seed is not reproducible)\n"); return 8; }
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    rc = pipeline_render_jitter(&s, place, win, w, h, ss, (uint64_t)seed, buf);
    if (rc != 0) { fprintf(stderr, "jitter render rejected: code %d\n", rc); free(buf); return 6; }
    if (export_pgm(argv[3], buf, w, h) != 0) { free(buf); return 7; }
    printf("jitter seed %llu ss %d polys %d verts %d on %d\n",
           seed, ss, s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}

/* cli sequence out.txt seed n */
static int cmd_sequence(char **argv) {
    unsigned long long seed = strtoull(argv[3], NULL, 10);
    int n = atoi(argv[4]);
    if (seed == 0 || n <= 0) {
        fprintf(stderr, "rejected: seed must be nonzero and n must be positive\n");
        return 8;
    }
    int rc = jitter_export(argv[2], (uint64_t)seed, n);
    if (rc != 0) { fprintf(stderr, "sequence export failed: %d\n", rc); return 7; }
    printf("exported %d pairs seed %llu -> %s\n", n, seed, argv[2]);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2) {
        if ((!strcmp(argv[1], "jitter") && argc == 9)) return cmd_jitter(argv);
        if ((!strcmp(argv[1], "sequence") && argc == 5)) return cmd_sequence(argv);
    }
    if (argc == 6) return cmd_render(argv);
    fprintf(stderr,
            "usage:\n"
            "  %s scene.txt out.pgm w h scale\n"
            "  %s jitter scene.txt out.pgm w h scale ss seed\n"
            "  %s sequence out.txt seed n\n",
            argv[0], argv[0], argv[0]);
    return 1;
}
