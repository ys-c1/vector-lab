#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/simplify.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static void usage(const char *prog) {
    fprintf(stderr,
            "usage: %s scene.txt out.pgm w h scale\n"
            "       %s scene.txt out.pgm w h scale --simplify MAXDEV [--dump pts.txt]\n"
            "  --simplify MAXDEV  decimate each scene ring so its deviation stays\n"
            "                     within MAXDEV, solving the tolerance by bisection\n"
            "  --dump pts.txt     write the kept points of the simplified rings\n",
            prog, prog);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Legacy path: unchanged full export.  Any edit here changes old output. */
static int run_legacy(const char *in, const char *out, int w, int h, double sc) {
    FILE *f = fopen(in, "r");
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
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(out, buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d on %d\n", s.npolys, scene_count_verts(&s),
           export_count_on(buf, w, h));
    free(buf);
    return 0;
}

/* Reads one polygon after its "p" tag.  Unlike the legacy reader this has no
 * fixed point limit: the caller decimates down to the scene capacity. */
static int read_poly(FILE *f, RPoint **pts, int *cap, int *n) {
    *n = 0;
    for (;;) {
        double x, y;
        if (fscanf(f, "%lf %lf", &x, &y) != 2) break;
        if (*n >= *cap) {
            int ncap = *cap ? *cap * 2 : 256;
            RPoint *np = (RPoint*)realloc(*pts, sizeof(RPoint) * (size_t)ncap);
            if (!np) return -1;
            *pts = np;
            *cap = ncap;
        }
        (*pts)[(*n)++] = (RPoint){x, y};
        int c = fgetc(f);
        if (c == '\n' || c == EOF) break;
        ungetc(c, f);
    }
    return 0;
}

static int run_simplified(const char *in, const char *out, int w, int h,
                          double sc, double maxdev, const char *dump_path) {
    if (maxdev < 0.0 || maxdev != maxdev) {
        fprintf(stderr, "reject: tolerance %g is negative or not a number\n", maxdev);
        return 2;
    }
    FILE *f = fopen(in, "r");
    if (!f) { fprintf(stderr, "open scene failed\n"); return 2; }
    FILE *dump = dump_path ? fopen(dump_path, "w") : 0;
    if (dump_path && !dump) { fclose(f); fprintf(stderr, "open dump failed\n"); return 2; }

    RScene s;
    scene_init(&s);
    RPoint *pts = 0;
    int cap = 0, n = 0;
    int verts_in = 0, verts_out = 0, rejected = 0;
    double worst_dev = 0.0, last_tol = 0.0;
    double t0 = now_ms();

    char tag[16];
    while (fscanf(f, "%15s", tag) == 1) {
        if (tag[0] != 'p') break;
        if (read_poly(f, &pts, &cap, &n) != 0) {
            fprintf(stderr, "reject: out of memory while reading a polygon\n");
            rejected = 1;
            break;
        }
        verts_in += n;
        RPoint *kept = (RPoint*)malloc(sizeof(RPoint) * (size_t)(n ? n : 1));
        if (!kept) { rejected = 1; fprintf(stderr, "reject: out of memory\n"); break; }
        RSimplifyReport rep;
        const char *why = 0;
        int rc = simplify_decimate(pts, n, 1, maxdev, kept, n, &rep, &why);
        if (rc != RSIMP_OK) {
            fprintf(stderr, "reject: polygon %d: %s (code %d)\n", s.npolys, why ? why : "?", rc);
            free(kept);
            rejected = 1;
            break;
        }
        if (scene_add_poly(&s, kept, rep.kept) != 0) {
            fprintf(stderr,
                    "reject: polygon %d kept %d points, scene capacity is 64\n",
                    s.npolys, rep.kept);
            free(kept);
            rejected = 1;
            break;
        }
        if (dump) {
            fprintf(dump, "p\n");
            for (int i = 0; i < rep.kept; ++i)
                fprintf(dump, "%.6f %.6f\n", kept[i].x, kept[i].y);
        }
        verts_out += rep.kept;
        if (rep.max_deviation > worst_dev) worst_dev = rep.max_deviation;
        last_tol = rep.tolerance;
        free(kept);
    }
    fclose(f);
    if (dump) fclose(dump);
    free(pts);
    if (rejected) return 3;

    double elapsed = now_ms() - t0;
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 4;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 5;
    RPlace place = {0.0, 0.0, 0.0, sc};
    if (pipeline_render(&s, place, win, w, h, buf) != 0) { free(buf); return 6; }
    if (export_pgm(out, buf, w, h) != 0) { free(buf); return 7; }
    printf("polys %d verts %d (in %d, kept %.0f%%) on %d tol %.6g maxdev %.6g %.2fms\n",
           s.npolys, verts_out, verts_in,
           verts_in ? 100.0 * (double)verts_out / (double)verts_in : 100.0,
           export_count_on(buf, w, h), last_tol, worst_dev, elapsed);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 6) { usage(argv[0]); return 1; }
    const char *dump_path = 0;
    int simplify = 0;
    double maxdev = 0.0;
    for (int i = 6; i < argc; ++i) {
        if (!strcmp(argv[i], "--simplify")) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            char *end = 0;
            maxdev = strtod(argv[++i], &end);
            if (!end || *end || end == argv[i]) {
                fprintf(stderr, "reject: --simplify needs a numeric tolerance\n");
                return 1;
            }
            simplify = 1;
        } else if (!strcmp(argv[i], "--dump")) {
            if (i + 1 >= argc) { usage(argv[0]); return 1; }
            dump_path = argv[++i];
        } else {
            fprintf(stderr, "reject: unknown option %s\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }
    int w = atoi(argv[3]), h = atoi(argv[4]);
    double sc = atof(argv[5]);
    if (simplify)
        return run_simplified(argv[1], argv[2], w, h, sc, maxdev, dump_path);
    if (dump_path) {
        fprintf(stderr, "reject: --dump requires --simplify\n");
        return 1;
    }
    return run_legacy(argv[1], argv[2], w, h, sc);
}
