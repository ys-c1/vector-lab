#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [tile]\n", argv[0]);
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
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (argc >= 7) {
        int tile = atoi(argv[6]);
        TileCache tc;
        if (tc_init(&tc, tile, tile) != 0) { /* tile<=0 不许进 */
            fprintf(stderr, "bad tile size\n");
            free(buf);
            return 8;
        }
        int rc = pipeline_render_tc(&s, place, win, w, h, buf, &tc);
        fprintf(stderr, "cache hits %ld misses %ld collisions %ld\n",
                tc.hits, tc.misses, tc.collisions);
        tc_free(&tc);
        if (rc != 0) { free(buf); return 6; }
    } else {
        if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    free(buf);
    return 0;
}
