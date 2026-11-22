#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return -1; }
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

static void usage(const char *prog) {
    fprintf(stderr,
        "usage:\n"
        "  %s scene.txt out.pgm w h scale          # legacy: fixed ss=4, binary\n"
        "  %s --budget N scene.txt out.pgm [--ref R]\n"
        "      allocate (w,h,ss) within N samples, render coverage PGM\n"
        "  %s --check w h ss budget                # is this split within budget?\n",
        prog, prog, prog);
}

int main(int argc, char **argv) {
    if (argc >= 2 && strcmp(argv[1], "--check") == 0) {
        if (argc < 6) { usage(argv[0]); return 1; }
        int w = atoi(argv[2]), h = atoi(argv[3]), ss = atoi(argv[4]);
        long budget = atol(argv[5]);
        char msg[256];
        int rc = ralloc_check(w, h, ss, budget, msg, sizeof(msg));
        printf("check %dx%d ss=%d budget=%ld: %s\n", w, h, ss, budget, msg);
        return rc == 0 ? 0 : 2;
    }

    if (argc >= 2 && strcmp(argv[1], "--budget") == 0) {
        if (argc < 5) { usage(argv[0]); return 1; }
        long budget = atol(argv[2]);
        const char *scene_path = argv[3];
        const char *out_path = argv[4];
        int ref_side = 0;
        for (int i = 5; i < argc; ++i) {
            if (strcmp(argv[i], "--ref") == 0 && i + 1 < argc) ref_side = atoi(argv[++i]);
            else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 1; }
        }
        RScene s;
        if (load_scene(scene_path, &s) != 0) return 2;

        if (budget <= 0) {
            /* 预算为零要明确报出，不静默出图。 */
            printf("budget %ld: BUDGET_ZERO (no image written)\n", budget);
            return 3;
        }

        RAlloc best;
        RAllocReport rep;
        int rc = ralloc_solve(&s, NULL, budget, ref_side, &best, &rep);
        if (rc == RALLOC_NO_FIT) {
            printf("budget %ld: NO_FIT (smallest candidate needs more than budget)\n", budget);
            return 4;
        }
        if (rc != RALLOC_OK) {
            printf("budget %ld: error %d\n", budget, rc);
            return 5;
        }

        RBox win;
        if (scene_bbox(&s, &win) != 0) return 6;
        unsigned char *buf = (unsigned char *)malloc((size_t)best.w * best.h);
        if (!buf) return 7;
        RPlace place = {0.0, 0.0, 0.0, 1.0};
        if (pipeline_render_ss(&s, place, win, best.w, best.h, best.ss, buf) != 0) {
            free(buf);
            return 8;
        }
        if (export_pgm(out_path, buf, best.w, best.h) != 0) { free(buf); return 9; }
        printf("budget %ld w %d h %d ss %d cost %ld jag %.6f sizes %d cand %d eval %d ms %.1f\n",
               budget, best.w, best.h, best.ss, best.cost, best.jag,
               rep.sizes, rep.candidates, rep.evaluated, rep.plan_ms + rep.eval_ms);
        free(buf);
        return 0;
    }

    if (argc < 6) { usage(argv[0]); return 1; }
    if (argc != 6) { usage(argv[0]); return 1; }
    RScene s;
    if (load_scene(argv[1], &s) != 0) return 2;
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
