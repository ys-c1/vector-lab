#include "src/scene.h"
#include "src/pipeline.h"
#include "src/fpconf.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 用法（前 5 个位置参数向后兼容）：
 *   cli scene.txt out.pgm w h scale [--rot R] [--conf fp.conf]
 * 不传 --rot 时 rot=0；不传 --conf 时使用基线配置（等价旧行为）。
 * 命名选项可出现在位置参数之后任意位置，顺序不限。
 */
int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [--rot R] [--conf fp.conf]\n", argv[0]);
        return 1;
    }
    double rot = 0.0;
    const char *confpath = NULL;
    for (int a = 6; a < argc; ++a) {
        if (!strncmp(argv[a], "--rot=", 6)) {
            rot = atof(argv[a] + 6);
        } else if (!strcmp(argv[a], "--rot") && a + 1 < argc) {
            rot = atof(argv[++a]);
        } else if (!strncmp(argv[a], "--conf=", 7)) {
            confpath = argv[a] + 7;
        } else if (!strcmp(argv[a], "--conf") && a + 1 < argc) {
            confpath = argv[++a];
        } else {
            fprintf(stderr, "REJECT unknown argument '%s'\n", argv[a]);
            return 1;
        }
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

    /* 配置：显式给 --conf 则严格校验；缺省则为基线（与历史行为一致）。 */
    RFPConf cfg;
    if (confpath) {
        char err[256] = {0};
        int rc = fpconf_parse(confpath, &cfg, err, sizeof err);
        if (rc != 0) {
            fprintf(stderr, "REJECT %s (rc=%d)\n", err[0] ? err : "invalid config", rc);
            return 8;
        }
    } else {
        cfg.fp_deterministic = 0;
        cfg.fma = 1;
        cfg.eval_order = 0;
    }

    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, rot, sc};
    int rc = pipeline_render_cfg(&s, place, win, w, h, &cfg, buf);
    if (rc != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }

    char snap[160];
    fpconf_snapshot_str(&cfg, snap, sizeof snap);
    printf("polys %d verts %d on %d | %s\n",
           s.npolys, scene_count_verts(&s), export_count_on(buf, w, h), snap);
    free(buf);
    return 0;
}
