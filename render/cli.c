#include "src/scene.h"
#include "src/pipeline.h"
#include "src/margin.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [fit]\n", argv[0]);
        return 1;
    }
    int fit = argc >= 7 && strcmp(argv[6], "fit") == 0;
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
    RBox win = {0, 0, 1, 1};
    if (scene_bbox(&s, &win) != 0 && !fit) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (fit) {
        RMargin m;
        int rc = pipeline_render_fit(&s, place, win, w, h, buf, &m);
        if (rc == RMARGIN_EMPTY) { printf("empty scene\n"); free(buf); return 8; }
        if (rc == RMARGIN_REJECT) {
            fprintf(stderr, "margin L%d R%d B%d T%d over 1/10 of canvas, rejected\n",
                    m.left, m.right, m.bottom, m.top);
            free(buf);
            return 9;
        }
        if (rc != 0) { free(buf); return 6; }
        printf("margin L%d R%d B%d T%d\n", m.left, m.right, m.bottom, m.top);
    } else {
        if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
