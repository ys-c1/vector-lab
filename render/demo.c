#define _POSIX_C_SOURCE 200809L
#include "scene.h"
#include "pipeline.h"
#include "raster.h"
#include "jitter.h"
#include "export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 64
#define H 64
#define SS 4
#define SEED 20261123ULL
#define RUNS 10
#define NANG 4

static const double ANG_DEG[NANG] = {15.0, 30.0, 45.0, 60.0};
static const double PI = 3.14159265358979323846;

/* Staircase steps: mixed 2x2 blocks whose four pixels are all binary
 * (0 or 255). A slanted edge crossing a binary raster produces exactly
 * these hard corner blocks — the visible "staircase". Grayscale coverage
 * turns the same blocks into soft transitions, which do not read as steps.
 * Both schemes are scored with the identical function. */
static int staircase_steps(const unsigned char *buf, int w, int h) {
    int n = 0;
    for (int y = 0; y + 1 < h; ++y)
        for (int x = 0; x + 1 < w; ++x) {
            unsigned char v[4] = {
                buf[y * w + x], buf[y * w + x + 1],
                buf[(y + 1) * w + x], buf[(y + 1) * w + x + 1]
            };
            int binary = 1;
            for (int i = 0; i < 4; ++i)
                if (v[i] != 0 && v[i] != 255) { binary = 0; break; }
            int mixed = !(v[0] == v[1] && v[1] == v[2] && v[2] == v[3]);
            if (binary && mixed) ++n;
        }
    return n;
}

static long ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main(void) {
    RPoint sq[4] = {{-5, -5}, {5, -5}, {5, 5}, {-5, 5}};
    RScene s;
    scene_init(&s);
    if (scene_add_poly(&s, sq, 4) != 0) return 1;
    RBox win = {-8, -8, 8, 8};
    size_t nbytes = (size_t)W * H;
    unsigned char *grid = malloc(nbytes);
    unsigned char *ref = malloc(nbytes);
    unsigned char *cur = malloc(nbytes);
    if (!grid || !ref || !cur) return 2;

    /* Sequence export for independent item-by-item comparison (gold ref). */
    int seq_len = SS * SS;
    if (jitter_export("demo-seq.txt", SEED, seq_len) != 0) {
        fprintf(stderr, "sequence export failed\n");
        return 3;
    }

    printf("seed=%llu ss=%d seq=%d runs=%d image=%dx%d\n",
           (unsigned long long)SEED, SS, seq_len, RUNS, W, H);
    printf("%-6s %-12s %-12s %-9s %-10s %s\n",
           "angle", "steps_equal", "steps_jitter", "reduct", "10x_same", "ms_jitter");

    int all_pass = 1;
    for (int a = 0; a < NANG; ++a) {
        RPlace place = {0.0, 0.0, ANG_DEG[a] * PI / 180.0, 1.0};

        if (pipeline_render(&s, place, win, W, H, grid) != 0) return 4;
        int steps_eq = staircase_steps(grid, W, H);

        long t0 = ms_now();
        int same = 1;
        for (int r = 0; r < RUNS; ++r) {
            if (pipeline_render_jitter(&s, place, win, W, H, SS, SEED, cur) != 0)
                return 5;
            if (r == 0) memcpy(ref, cur, nbytes);
            else if (memcmp(ref, cur, nbytes) != 0) same = 0;
        }
        long ms = ms_now() - t0;

        int steps_jit = staircase_steps(ref, W, H);
        double reduct = 100.0 * (steps_eq - steps_jit) / (double)steps_eq;
        if (reduct < 30.0) all_pass = 0;

        char name_eq[32], name_jit[32];
        snprintf(name_eq, sizeof name_eq, "demo-eq-%d.pgm", (int)ANG_DEG[a]);
        snprintf(name_jit, sizeof name_jit, "demo-jit-%d.pgm", (int)ANG_DEG[a]);
        export_pgm(name_eq, grid, W, H);
        export_pgm(name_jit, ref, W, H);

        printf("%-6.0f %-12d %-12d %-8.1f%% %-10s %ld\n",
               ANG_DEG[a], steps_eq, steps_jit, reduct,
               same ? "IDENTICAL" : "DIFFER", ms);
    }

    /* Rejection cases: zero seed and zero-length sequence must be refused. */
    int rej_seed = (pipeline_render_jitter(&s, (RPlace){0,0,0,1}, win, W, H,
                                           SS, 0ULL, cur) == -5);
    RJitter tmp[1];
    int rej_len = (jitter_gen(SEED, 0, tmp) == -1);
    printf("reject zero-seed: %s\n", rej_seed ? "REFUSED(-5)" : "ACCEPTED-BUG");
    printf("reject zero-length: %s\n", rej_len ? "REFUSED(-1)" : "ACCEPTED-BUG");
    printf("30%% reduction at all %d angles: %s\n", NANG, all_pass ? "PASS" : "FAIL");

    free(grid); free(ref); free(cur);
    return (all_pass && rej_seed && rej_len) ? 0 : 6;
}
