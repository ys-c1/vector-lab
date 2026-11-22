#define _DEFAULT_SOURCE
#include "contour.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* 两档点数：small = 4 层 × 64 点，large = 8 层 × 1024 点。
 * 每层为正 n 边形，半径随层递减 => 嵌套、面积随层号单调不增。
 * 计时只含解析 + 校验 + 面积 + 单调性（不含造数据）。*/

typedef struct { int layers; int per; int reps; } Tier;

static const double PI = 3.14159265358979323846;

static int gen_input(const char *path, int layers, int per) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int l = 0; l < layers; ++l) {
        double r = 100.0 - (double)l * (100.0 / (double)(layers + 1));
        fprintf(f, "L %d\n", l);
        for (int i = 0; i < per; ++i) {
            double a = 2.0 * PI * (double)i / (double)per;
            fprintf(f, "%.6f %.6f\n", r * cos(a), r * sin(a));
        }
    }
    fclose(f);
    return 0;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static int run_tier(const char *dir, const Tier *t) {
    char path[512];
    snprintf(path, sizeof(path), "%s/bench-%d.txt", dir, t->per);
    if (gen_input(path, t->layers, t->per) != 0) {
        perror("gen"); return 1;
    }

    double best = 1e300;
    CTSet set;
    CTError err;
    double *areas = malloc(sizeof(double) * (size_t)t->layers);
    int rc = 0;
    long peak = 0, inb = 0;
    for (int rep = 0; rep < t->reps; ++rep) {
        double t0 = now_ms();
        rc = contour_load(path, &set, &err);
        if (rc == CT_OK) rc = contour_validate(&set, &err);
        if (rc == CT_OK) {
            for (int l = 0; l < set.layers; ++l)
                areas[l] = contour_area(set.cs[l].pts, set.cs[l].count);
            rc = contour_check_mono(&set, areas, &err);
        }
        double dt = now_ms() - t0;
        if (rc != CT_OK) break;
        if (rep == t->reps - 1) { peak = set.peak_bytes; inb = set.input_bytes; }
        if (dt < best) best = dt;
        contour_free(&set);
    }
    if (rc != CT_OK) {
        fprintf(stderr, "bench reject layer %d: %s\n", err.layer, ct_strerror(err.code));
        free(areas);
        return 1;
    }

    double ratio = inb ? (double)peak / (double)inb : 0.0;
    printf("%-6s layers=%d verts/layer=%-5d total=%-6d best=%8.3f ms  "
           "mem=%ld/%ld=%.2fx (limit 6x) %s\n",
           t->per == 64 ? "small" : "large",
           t->layers, t->per, t->layers * t->per, best,
           peak, inb, ratio, ratio > 6.0 ? "OVER-LIMIT" : "ok");
    free(areas);
    return ratio > 6.0 ? 1 : 0;
}

int main(void) {
    char dir[] = "/tmp/contour-bench-XXXXXX";
    if (!mkdtemp(dir)) { perror("mkdtemp"); return 2; }
    Tier tiers[2] = {
        { 4, 64, 50 },
        { 8, 1024, 5 },
    };
    int bad = 0;
    for (int i = 0; i < 2; ++i)
        bad |= run_tier(dir, &tiers[i]);
    return bad;
}
