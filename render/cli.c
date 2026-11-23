#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/guard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 6) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [guard=HEX|guard=off]\n", argv[0]);
        return 1;
    }
    int guard_on = 1;
    unsigned char sentinel = RGUARD_SENTINEL_A;
    if (argc >= 7) {
        if (!strcmp(argv[6], "guard=off")) {
            guard_on = 0;
        } else if (!strncmp(argv[6], "guard=0x", 8)) {
            sentinel = (unsigned char)strtoul(argv[6] + 8, NULL, 16);
        } else if (!strncmp(argv[6], "guard=", 6)) {
            sentinel = (unsigned char)strtoul(argv[6] + 6, NULL, 16);
        } else {
            fprintf(stderr, "bad guard option\n");
            return 1;
        }
    }
    rguard_configure(guard_on, sentinel);
    rguard_reset();
    FILE *f = fopen(argv[1], "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
    RScene s;
    rguard_buffer(&s, sizeof s); /* sentinel behind whatever parsing leaves unset */
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
    rguard_buffer(buf, (size_t)w * h);
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s), export_count_on(buf, w, h));
    /* Guard statistics go to stderr so stdout (and the image) are unchanged. */
    fprintf(stderr, "guard %s sentinel 0x%02X fills %llu bytes %llu\n",
            guard_on ? "on" : "off", (unsigned)sentinel,
            rguard_stats()->fill_ops, rguard_stats()->fill_bytes);
    free(buf);
    return 0;
}
