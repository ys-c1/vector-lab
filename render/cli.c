#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/gamma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_scene(const char *path, RScene *s) {
    scene_init(s);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
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
        if (scene_add_poly(s, pts, n) != 0) return -2;
    }
    fclose(f);
    return 0;
}

/* cli fit <src.pgm> <ref.pgm> <out.pgm>
 * 按参照灰度直方图反解 gamma，编码源图写 out，并报告 gamma/残差/耗时。 */
static int cmd_fit(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s fit src.pgm ref.pgm out.pgm\n", argv[0]);
        return 1;
    }
    unsigned char *src = NULL, *ref = NULL;
    int sw = 0, sh = 0, rw = 0, rh = 0;
    char field[GAM_FIELD_MAX] = "";
    int rc = gamma_load_pgm(argv[2], &src, &sw, &sh, field, sizeof field);
    if (rc != GAM_OK) {
        fprintf(stderr, "REJECT src: cannot read %s (%s)\n", argv[2], field);
        return 2;
    }
    rc = gamma_load_pgm(argv[3], &ref, &rw, &rh, field, sizeof field);
    if (rc != GAM_OK) {
        fprintf(stderr, "REJECT ref: cannot read %s (%s)\n", argv[3], field);
        free(src);
        return 2;
    }
    if (sw != rw || sh != rh) {
        fprintf(stderr, "REJECT ref: size %dx%d != src %dx%d (param: ref)\n",
                rw, rh, sw, sh);
        free(src); free(ref);
        return 2;
    }
    GammaFit fit;
    rc = gamma_fit(src, ref, sw, sh, &fit);
    if (rc == GAM_E_FLAT) {
        int src_flat = !strcmp(fit.err_field, "src");
        const char *path = src_flat ? argv[2] : argv[3];
        fprintf(stderr, "REJECT impossible: %s is uniform level %d (all %s), "
                "gamma undefined (param: %s)\n", path, fit.flat_level,
                fit.flat_level == 255 ? "white" : "black", fit.err_field);
        free(src); free(ref);
        return 3;
    }
    if (rc != GAM_OK) {
        fprintf(stderr, "REJECT: fit failed rc=%d (param: gamma)\n", rc);
        free(src); free(ref);
        return 3;
    }
    printf("fit gamma %.9f residual %.3e plateau [%.9f, %.9f] evals %d ms %.2f\n",
           fit.gamma, fit.residual, fit.lo, fit.hi, fit.evals, fit.ms);
    if (fit.residual > 1e-3) {
        fprintf(stderr, "REJECT gamma %.6f: residual %.3e > 1e-3\n",
                fit.gamma, fit.residual);
        free(src); free(ref);
        return 4;
    }
    if (gamma_apply(src, sw, sh, fit.gamma, src, field, sizeof field) != GAM_OK) {
        fprintf(stderr, "REJECT: encode failed (%s)\n", field);
        free(src); free(ref);
        return 4;
    }
    if (export_pgm(argv[4], src, sw, sh) != 0) {
        fprintf(stderr, "REJECT: cannot write %s\n", argv[4]);
        free(src); free(ref);
        return 4;
    }
    free(src); free(ref);
    return 0;
}

static int cmd_render(int argc, char **argv) {
    const char *gamma_arg = NULL;
    int linear = 0;
    const char *pos[8];
    int npos = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--gamma") && i + 1 < argc) {
            gamma_arg = argv[++i];
        } else if (!strcmp(argv[i], "--linear")) {
            gamma_arg = NULL;
            linear = 1;
        } else if (argv[i][0] == '-' && argv[i][1] == '-') {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 1;
        } else if (npos < 8) {
            pos[npos++] = argv[i];
        }
    }
    (void)linear;
    if (npos < 5) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale "
                "[--gamma G | --linear]\n", argv[0]);
        return 1;
    }
    RScene s;
    if (load_scene(pos[0], &s) != 0) {
        fprintf(stderr, "open scene failed\n");
        return 2;
    }
    RExportGamma opt = {0, 1.0};
    if (gamma_arg) {
        char field[GAM_FIELD_MAX] = "";
        double g = atof(gamma_arg);
        if (gamma_validate(g, field, sizeof field) != GAM_OK) {
            fprintf(stderr, "REJECT gamma=%s: must be positive and finite "
                    "(param: gamma)\n", gamma_arg);
            return 8;
        }
        opt.enabled = 1;
        opt.gamma = g;
    }
    int w = atoi(pos[2]), h = atoi(pos[3]);
    double sc = atof(pos[4]);
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    if (w <= 0 || h <= 0) { fprintf(stderr, "bad size\n"); return 4; }
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render_gamma(&s, place, win, w, h, buf, &opt) != 0) {
        free(buf);
        return 6;
    }
    if (export_pgm(pos[1], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s),
           export_count_on(buf, w, h));
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 2 && !strcmp(argv[1], "fit")) return cmd_fit(argc, argv);
    return cmd_render(argc, argv);
}
