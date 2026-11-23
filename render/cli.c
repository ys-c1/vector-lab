#include "src/scene.h"
#include "src/pipeline.h"
#include "src/backend.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

int main(int argc, char **argv) {
    if (argc < 6 || argc > 7) {
        fprintf(stderr, "usage: %s scene.txt out.pgm w h scale [backend]\n", argv[0]);
        fprintf(stderr, "  backend: %d=scanline (default), %d=tile\n",
                RASTER_BACKEND_SCANLINE, RASTER_BACKEND_TILE);
        return 1;
    }
    /* 后端开关：默认扫描线；给值必须严格为整数且落在合法档位。 */
    int backend = RASTER_BACKEND_SCANLINE;
    if (argc == 7) {
        errno = 0;
        char *end = NULL;
        long v = strtol(argv[6], &end, 10);
        if (errno != 0 || end == argv[6] || *end != '\0' ||
            v < RASTER_BACKEND_MIN || v > RASTER_BACKEND_MAX) {
            fprintf(stderr,
                    "backend switch out of range: '%s' (valid: %d..%d)\n",
                    argv[6], RASTER_BACKEND_MIN, RASTER_BACKEND_MAX);
            return 8;
        }
        backend = (int)v;
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
    int rc = pipeline_render_backend(backend, &s,
                                     (RPlace){0.0, 0.0, 0.0, sc},
                                     win, w, h, buf);
    if (rc != 0) {
        fprintf(stderr, "render failed: backend=%d rc=%d\n", backend, rc);
        free(buf);
        return 6;
    }
    if (export_pgm(argv[2], buf, w, h) != 0) { free(buf); return 7; }
    printf("backend %d polys %d verts %d on %d\n",
           backend, s.npolys, scene_count_verts(&s),
           export_count_on(buf, w, h));
    free(buf);
    return 0;
}
