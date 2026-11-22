#define _POSIX_C_SOURCE 199309L
#include "simplify.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#define REPS 20000

static int load(const char *path, HPoint *pts) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) ++n;
    fclose(f);
    return n;
}

static const char *errname(int rc) {
    switch (rc) {
        case -1: return "bad args";
        case -2: return "bad ratio";
        case -3: return "fewer than 3 input points";
        case -4: return "strict hull has fewer than 3 vertices: rejected";
        default: return "?";
    }
}

static int run_one(const char *tag, const char *path, double ratio) {
    HPoint pts[64];
    int n = load(path, pts);
    if (n < 0) { fprintf(stderr, "open %s failed\n", path); return 1; }

    HSimp r;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = 0;
    for (int k = 0; k < REPS; ++k) rc = hull_simplify(pts, n, ratio, &r);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double us = ((t1.tv_sec - t0.tv_sec) * 1e6 +
                 (t1.tv_nsec - t0.tv_nsec) / 1e3) / REPS;

    printf("[%s] target ratio %.4f, input %d points (%s)\n",
           tag, ratio, n, path);
    if (rc != 0) {
        printf("  REJECTED rc=%d (%s)\n\n", rc, errname(rc));
        return 0;
    }
    printf("  collinear removed : %d\n", r.collinear);
    printf("  strict hull verts : %d\n", r.hull_count);
    printf("  analytic estimate : %d verts\n", r.est_count);
    printf("  kept verts        : %d\n", r.kept);
    printf("  area full / kept  : %.6f / %.6f\n", r.area_full, r.area_kept);
    printf("  area ratio        : %.6f (target %.4f, %s)\n",
           r.ratio, ratio, r.ratio + 1e-12 >= ratio ? "OK" : "BELOW TARGET");
    printf("  time per call     : %.3f us (avg of %d)\n\n", us, REPS);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "data";
    char path[256];
    double ratios[4] = {0.99, 0.95, 0.90, 0.80};
    const char *names[4] = {"circle", "ellipse", "blob", "rect_bulge"};
    for (int s = 0; s < 4; ++s) {
        snprintf(path, sizeof(path), "%s/%s.txt", dir, names[s]);
        if (run_one(names[s], path, ratios[s])) return 1;
    }
    /* 退化点集：全共线，必须拒并 */
    snprintf(path, sizeof(path), "%s/flat.txt", dir);
    if (run_one("flat(reject)", path, 0.90)) return 1;
    return 0;
}
