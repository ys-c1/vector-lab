/* 凸层（onion peeling）演示：
 *   demo gen <outdir>          生成四组点集 random/cluster/collinear/twocluster
 *   demo run <points.txt> <target> <out.pgm> [layers.txt] [w h]
 *
 * run 输出一行报告：反解层号、剩余面积占比、耗时（毫秒），
 * 并把 0..反解层各层轮廓按层号灰度写入 PGM。 */
#define _POSIX_C_SOURCE 199309L
#include "hull.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W_DEF 240
#define H_DEF 240
#define MARGIN 0.02
#define MAXN 256

/* 确定性 LCG，保证四组点集可复现。 */
static unsigned long rng_state;
static double rng_next(void) {
    rng_state = rng_state * 6364136223846793005UL + 1442695040888963407UL;
    return (double)(rng_state >> 33) / (double)(1UL << 31);
}

static int gen_one(const char *path, int kind) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return -1; }
    int n = 0;
    switch (kind) {
    case 0: { /* 随机：160 个均匀点 */
        rng_state = 20261123UL; n = 160;
        for (int i = 0; i < n; ++i)
            fprintf(f, "%.6f %.6f\n", rng_next() * 100.0, rng_next() * 100.0);
        break;
    }
    case 1: { /* 聚集：一个密集团（中心 50,50）加少量散点 */
        rng_state = 424242UL; n = 160;
        for (int i = 0; i < n - 12; ++i) {
            double r = rng_next() * 12.0, t = rng_next() * 6.2831853;
            fprintf(f, "%.6f %.6f\n", 50.0 + r * cos(t), 50.0 + r * sin(t));
        }
        for (int i = 0; i < 12; ++i)
            fprintf(f, "%.6f %.6f\n", rng_next() * 100.0, rng_next() * 100.0);
        break;
    }
    case 2: { /* 共线：三层同心三角外壳 + 中心共线核心（2 点，剥完不足三点） */
        n = 0;
        double rad[3] = {40.0, 25.0, 12.0};
        for (int t = 0; t < 3; ++t)
            for (int v = 0; v < 3; ++v) {
                double ang = 6.2831853 * v / 3.0 + 1.5707963;
                fprintf(f, "%.6f %.6f\n",
                        50.0 + rad[t] * cos(ang), 50.0 + rad[t] * sin(ang));
                ++n;
            }
        fprintf(f, "%.6f %.6f\n", 48.0, 50.0); ++n;
        fprintf(f, "%.6f %.6f\n", 52.0, 50.0); ++n;
        break;
    }
    case 3: { /* 双簇：两个分离的密集团 */
        rng_state = 9090909UL; n = 180;
        for (int c = 0; c < 2; ++c) {
            double cx = c ? 80.0 : 20.0, cy = 50.0;
            for (int i = 0; i < n / 2; ++i) {
                double r = rng_next() * 8.0, t = rng_next() * 6.2831853;
                fprintf(f, "%.6f %.6f\n", cx + r * cos(t), cy + r * sin(t));
            }
        }
        break;
    }
    }
    fclose(f);
    return n;
}

static int cmd_gen(const char *outdir) {
    const char *names[4] = {"random", "cluster", "collinear", "twocluster"};
    for (int k = 0; k < 4; ++k) {
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.txt", outdir, names[k]);
        int n = gen_one(path, k);
        if (n < 0) return 1;
        printf("gen %-10s %3d points -> %s\n", names[k], n, path);
    }
    return 0;
}

static int load_points(const char *path, HPoint *pts, int cap) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    int n = 0;
    while (n < cap && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) ++n;
    fclose(f);
    return n;
}

static int write_layers(const char *path, const LResult *res, int klayer) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return -1; }
    for (int i = 0; i <= klayer; ++i) {
        fprintf(f, "L%d %d\n", i, res->layers[i].count);
        for (int j = 0; j < res->layers[i].count; ++j)
            fprintf(f, "%.6f %.6f\n", res->layers[i].pts[j].x,
                    res->layers[i].pts[j].y);
    }
    fclose(f);
    return 0;
}

static int cmd_run(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: demo run <points.txt> <target> <out.pgm> "
                        "[layers.txt] [w h]\n");
        return 2;
    }
    const char *ppath = argv[2];
    double target = atof(argv[3]);
    const char *pgm = argv[4];
    const char *lpath = argc >= 6 ? argv[5] : NULL;
    int w = argc >= 8 ? atoi(argv[6]) : W_DEF;
    int h = argc >= 8 ? atoi(argv[7]) : H_DEF;
    if (target <= 0.0 || target >= 1.0) {
        fprintf(stderr, "REJECT target must be in (0,1): %g\n", target);
        return 3;
    }

    HPoint pts[MAXN];
    int n = load_points(ppath, pts, MAXN);
    if (n < 1) { fprintf(stderr, "REJECT no points loaded\n"); return 3; }

    LResult res;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = hull_layers(pts, n, &res);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
                (t1.tv_nsec - t0.tv_nsec) / 1.0e6;
    if (rc == -2) {
        fprintf(stderr, "REJECT layer count exceeds HULL_MAX_LAYERS=%d\n",
                HULL_MAX_LAYERS);
        return 4;
    }
    if (rc != 0) { fprintf(stderr, "REJECT hull_layers failed rc=%d\n", rc); return 4; }

    printf("points=%d total_layers=%d remaining=%d\n",
           n, res.nlayers, res.nrem);

    /* 退化：一层都剥不出来（如全共线）。 */
    if (res.nlayers == 0) {
        fprintf(stderr, "REJECT degenerate set: no convex layer can be "
                        "formed, %d points remain (need >=3 non-collinear)\n",
                res.nrem);
        return 5;
    }

    int k = hull_solve_layer(&res, target);
    if (k == -3) {
        fprintf(stderr,
                "REJECT target %.4f not reached after %d layers "
                "(layer number cap = total layers = %d); "
                "remaining ratio=%.6f\n",
                target, res.nlayers, res.nlayers,
                hull_poly_area(&res.layers[res.nlayers - 1]) /
                hull_poly_area(&res.layers[0]));
        if (res.nrem < 3)
            fprintf(stderr, "NOTE peeling exhausted with only %d points "
                            "left (<3)\n", res.nrem);
        return 6;
    }
    if (k < 0) { fprintf(stderr, "REJECT solve failed rc=%d\n", k); return 4; }

    double base = hull_poly_area(&res.layers[0]);
    double ratio = hull_poly_area(&res.layers[k]) / base;

    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 7;
    if (hull_layers_pgm(&res, k, w, h, MARGIN, buf) != 0) {
        free(buf);
        fprintf(stderr, "REJECT pgm render failed\n");
        return 4;
    }
    FILE *f = fopen(pgm, "wb");
    if (!f) { perror(pgm); free(buf); return 7; }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * h, f);
    fclose(f);
    free(buf);

    if (lpath && write_layers(lpath, &res, k) != 0) return 7;

    printf("solved_layer=%d remaining_area_ratio=%.6f "
           "elapsed_ms=%.3f pgm=%s\n", k + 1, ratio, ms, pgm);
    if (!(ratio < target))
        fprintf(stderr, "INTERNAL ERROR ratio %.6f not below target %.4f\n",
                ratio, target);
    if (res.nrem < 3)
        printf("NOTE only %d points left after peeling (<3)\n", res.nrem);
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "gen"))
        return cmd_gen(argv[2]);
    if (argc >= 2 && !strcmp(argv[1], "run"))
        return cmd_run(argc, argv);
    fprintf(stderr, "usage: %s gen <outdir>\n"
                    "       %s run <points.txt> <target> <out.pgm> "
                    "[layers.txt] [w h]\n", argv[0], argv[0]);
    return 1;
}
