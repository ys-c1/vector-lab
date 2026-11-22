#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "../winfit/src/winfit.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc != 6 && argc != 11) {
        fprintf(stderr,
                "usage (固定窗口): %s scene.txt out.pgm w h scale\n"
                "usage (反解窗口): %s scene.txt out.pgm w h scale lo hi rho_t ux uy\n",
                argv[0], argv[0]);
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

    if (argc == 11) {
        /* 新逻辑：按目标占屏区间反解窗口四边 */
        double lo = atof(argv[6]), hi = atof(argv[7]);
        double rho_t = atof(argv[8]), ux = atof(argv[9]), uy = atof(argv[10]);
        WFBox bb = {win.minx, win.miny, win.maxx, win.maxy};
        WFBox fitted;
        int rc = winfit_fit(&bb, lo, hi, rho_t, ux, uy, &fitted);
        if (rc != WF_OK) {
            fprintf(stderr, "winfit 拒绝: %s\n", winfit_strerror(rc));
            return 8;
        }
        if (winfit_in_range(&bb, &fitted, lo, hi) != 1) {
            fprintf(stderr, "winfit 拒绝: 反解占屏比例未落入给定区间\n");
            return 9;
        }
        win.minx = fitted.minx; win.miny = fitted.miny;
        win.maxx = fitted.maxx; win.maxy = fitted.maxy;
        printf("fitted window [%.6f %.6f %.6f %.6f] occupancy %.4f%%\n",
               win.minx, win.miny, win.maxx, win.maxy,
               winfit_occupancy(&bb, &fitted) * 100.0);
    }
    /* argc == 6 时 win 保持 scene_bbox 原值：固定窗口老路径，逻辑零改动 */

    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
