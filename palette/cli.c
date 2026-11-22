/* 聚类调色板命令行。
 *
 * 旧逻辑（手挑调色板，开关关闭时走这条路径，输出与冻结基线逐字节一致）：
 *   palette legacy <in.pgm|ppm> <out.ppm>
 *
 * 新逻辑（聚类定色）：
 *   palette solve <in.pgm|ppm> [k]        单图；默认 k=8
 *   palette batch <manifest> [k]          清单每行：name path
 *   palette gen <dir>                     生成四类图与单色图（确定性 LCG）
 *
 * 退出码：0 成功；1 用法错；2 读文件失败；3 图像非法；4 k 非法（<=0）；
 *         5 k 超上限；6 单色图（单独报告）；7 写文件失败；8 方差未达标。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/palette.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1.0e9;
}

static const char *status_name(PStatus st) {
    switch (st) {
        case P_OK: return "OK";
        case P_EMPTY: return "EMPTY";
        case P_K_INVALID: return "K-ZERO";
        case P_K_TOO_BIG: return "K-TOO-BIG";
        case P_SINGLE_COLOR: return "SINGLE-COLOR";
        case P_IMG_BAD: return "IMAGE-BAD";
    }
    return "?";
}

/* 聚类结果的一行报告。v_hand 为手挑基线方差，v_km 为聚类方差。 */
static void report_cluster(const char *name, const ClusterResult *r,
                           double v_hand, double elapsed_ms) {
    double red = v_hand > 0.0 ? (v_hand - r->var) / v_hand * 100.0 : 0.0;
    printf("[%s] groups=%d iters=%d var_hand=%.3f var_cluster=%.3f reduce=%.2f%% "
           "elapsed_ms=%.3f\n", name, r->k, r->iters, v_hand, r->var, red, elapsed_ms);
    for (int j = 0; j < r->k; ++j)
        printf("  group=%d rep=#%02x%02x%02x count=%ld var=%.3f\n",
               j, r->rep[j].r, r->rep[j].g, r->rep[j].b, r->count[j], r->gvar[j]);
}

static int process_solve(const char *name, const char *path, int k, int verbose,
                         int *out_rc) {
    double t0 = now_sec();
    PImage img;
    if (palette_load(path, &img) != 0) {
        printf("[%s] LOAD-FAIL path=%s\n", name, path);
        *out_rc = 2;
        return -1;
    }
    ClusterResult r;
    if (img.w <= 0 || img.h <= 0) {
        printf("[%s] REJECT EMPTY reason=空图：没有像素，组数为零，拒绝\n", name);
        palette_free(&img);
        *out_rc = 3;
        return -1;
    }
    PStatus st = palette_kmeans(&img, k, &r);
    double el = (now_sec() - t0) * 1000.0;
    if (st == P_SINGLE_COLOR) {
        printf("[%s] SINGLE-COLOR rep=#%02x%02x%02x pixels=%ld elapsed_ms=%.3f "
               "(单独报告：整幅图只有一种颜色，组数为 1，不做迭代)\n",
               name, r.rep[0].r, r.rep[0].g, r.rep[0].b, r.count[0], el);
        palette_free(&img);
        *out_rc = 6;
        return 0;
    }
    if (st != P_OK) {
        const char *why = "";
        if (st == P_EMPTY) why = "空图：没有像素，组数为零，拒绝";
        else if (st == P_K_INVALID) why = "组数为零：k 必须 >= 1，拒绝";
        else if (st == P_K_TOO_BIG) why = "组数超过上限：k 必须 <= 32，拒绝";
        printf("[%s] REJECT %s k=%d reason=%s\n", name, status_name(st), k, why);
        palette_free(&img);
        *out_rc = (st == P_K_INVALID) ? 4 : (st == P_K_TOO_BIG ? 5 : 3);
        return -1;
    }
    double v_hand = palette_legacy_var(&img);
    report_cluster(name, &r, v_hand, el);
    long pixels = (long)img.w * img.h;
    double red = v_hand > 0.0 ? (v_hand - r.var) / v_hand : 1.0;
    printf("[%s] target(reduce>=30%%)=%s pixels=%ld groups=%d\n",
           name, red >= 0.30 ? "MET" : "MISS", pixels, r.k);
    if (verbose) {
        for (int j = 0; j < r.k; ++j)
            printf("  group=%d sse=%.3f\n", j, r.gsse[j]);
    }
    palette_free(&img);
    *out_rc = red >= 0.30 ? 0 : 8;
    return 0;
}

static int cmd_legacy(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: palette legacy <in> <out.ppm>\n");
        return 1;
    }
    PImage img;
    if (palette_load(argv[2], &img) != 0) {
        fprintf(stderr, "load failed: %s\n", argv[2]);
        return 2;
    }
    const Palette *hp = palette_hand();
    PImage q;
    if (palette_apply(&img, hp->n, hp->v, &q) != 0) { palette_free(&img); return 3; }
    int rc = palette_save(argv[3], &q);
    if (rc != 0) { free(q.pix); palette_free(&img); return 7; }
    double v = palette_var_against(&img, hp->n, hp->v);
    printf("legacy colors=%d var=%.3f pixels=%d out=%s\n",
           hp->n, v, img.w * img.h, argv[3]);
    free(q.pix);
    palette_free(&img);
    return 0;
}

static int cmd_solve(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: palette solve <in> [k]\n");
        return 1;
    }
    int k = argc >= 4 ? atoi(argv[3]) : 8;
    int rc = 0;
    process_solve("img", argv[2], k, 1, &rc);
    return rc;
}

static int cmd_batch(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: palette batch <manifest> [k]\n");
        return 1;
    }
    int k = argc >= 4 ? atoi(argv[3]) : 8;
    FILE *f = fopen(argv[2], "r");
    if (!f) { fprintf(stderr, "manifest open failed: %s\n", argv[2]); return 2; }
    char line[512];
    int worst = 0;
    while (fgets(line, sizeof(line), f)) {
        char name[128], path[256];
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%127s %255s", name, path) != 2) {
            printf("BAD-LINE %s", line);
            if (!worst) worst = 1;
            continue;
        }
        int rc = 0;
        process_solve(name, path, k, 0, &rc);
        /* 单色图是单独报告，不算失败；但它会把最坏码抬到 6 就不对了。 */
        if (rc != 6 && rc > worst) worst = rc;
    }
    fclose(f);
    return worst;
}

/* ---------- 四类图的确定性生成器 ---------- */

typedef struct { unsigned int s; } Lcg;
static unsigned int lcg_next(Lcg *g) { g->s = 1664525u * g->s + 1013904223u; return g->s; }
static int lcg_int(Lcg *g, int lo, int hi) { return lo + (int)(lcg_next(g) % (unsigned)(hi - lo + 1)); }

static unsigned char clamp8(int v) { return (unsigned char)(v < 0 ? 0 : (v > 255 ? 255 : v)); }

/* 暗部细节：灰度 0..52 的窄带，带轻微色偏；手挑调色板在这里只有一个 48，
 * 暗部全糊成一格，这正是本任务要解决的问题。 */
static void gen_dark(PImage *im) {
    im->w = 48; im->h = 32;
    im->pix = (unsigned char*)malloc((size_t)im->w * im->h * 3);
    Lcg g = {7u};
    int n = 0;
    for (int y = 0; y < im->h; ++y)
        for (int x = 0; x < im->w; ++x) {
            int band = y * 8 / im->h;
            int base = 4 + band * 5 + (x * 6 / im->w) + lcg_int(&g, -2, 2);
            if (base < 0) base = 0;
            if (base > 52) base = 52;
            im->pix[n++] = clamp8(base + base * 30 / 100);
            im->pix[n++] = clamp8(base);
            im->pix[n++] = clamp8(base + 18);
        }
}

/* 渐变：从左到右 8..252 的线性亮度渐变，竖向带轻微色偏。 */
static void gen_gradient(PImage *im) {
    im->w = 64; im->h = 32;
    im->pix = (unsigned char*)malloc((size_t)im->w * im->h * 3);
    int n = 0;
    for (int y = 0; y < im->h; ++y)
        for (int x = 0; x < im->w; ++x) {
            int t = x * 244 / (im->w - 1);
            im->pix[n++] = clamp8(8 + t);
            im->pix[n++] = clamp8(10 + t + y * 2 / im->h);
            im->pix[n++] = clamp8(24 + t - y * 2 / im->h);
        }
}

/* 平坦分区：六块高饱和色块，块内 ±4 噪声；手挑色表只有 4 个主色能对上，
 * 其余被拉到最近的灰或错色，方差大。 */
static void gen_flat(PImage *im) {
    static const int blocks[6][3] = {
        {30, 90, 150}, {210, 60, 60}, {60, 150, 80},
        {235, 225, 200}, {90, 60, 140}, {20, 20, 24},
    };
    im->w = 48; im->h = 32;
    im->pix = (unsigned char*)malloc((size_t)im->w * im->h * 3);
    Lcg g = {13u};
    int n = 0;
    for (int y = 0; y < im->h; ++y)
        for (int x = 0; x < im->w; ++x) {
            int b = ((x * 3) / im->w + (y / (im->h / 2)) * 3) % 6;
            int d = lcg_int(&g, -4, 4);
            im->pix[n++] = clamp8(blocks[b][0] + d);
            im->pix[n++] = clamp8(blocks[b][1] + d);
            im->pix[n++] = clamp8(blocks[b][2] + d);
        }
}

/* 混合：上半天空渐变带零星亮光，下半草地渐变带暗块；两类分布跨越明暗。 */
static void gen_mixed(PImage *im) {
    im->w = 48; im->h = 32;
    im->pix = (unsigned char*)malloc((size_t)im->w * im->h * 3);
    Lcg g = {17u};
    int n = 0;
    for (int y = 0; y < im->h; ++y)
        for (int x = 0; x < im->w; ++x) {
            int r, gg, b;
            if (y < im->h / 2) {
                int t = y * 60 / (im->h / 2 - 1);
                r = 210 - t; gg = 220 - t; b = 245 - t;
                if ((x * 7 + y * 3) % 23 == 0) { r = 250; gg = 248; b = 200; }
            } else {
                int t = (y - im->h / 2) * 70 / (im->h - im->h / 2 - 1);
                r = 30 + t; gg = 45 + t - 15; b = 25 + t - 15;
                if ((x / 5 + y / 3) % 4 == 0) { r += 25; gg += 20; b += 10; }
            }
            int d = lcg_int(&g, -5, 5);
            im->pix[n++] = clamp8(r + d);
            im->pix[n++] = clamp8(gg + d);
            im->pix[n++] = clamp8(b + d);
        }
}

/* 单色：整幅同一种颜色（单独报告用例）。 */
static void gen_solid(PImage *im) {
    im->w = 16; im->h = 16;
    im->pix = (unsigned char*)malloc((size_t)im->w * im->h * 3);
    for (int i = 0; i < im->w * im->h; ++i) {
        im->pix[3*i] = 72; im->pix[3*i + 1] = 72; im->pix[3*i + 2] = 72;
    }
}

static int cmd_gen(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: palette gen <dir>\n");
        return 1;
    }
    const char *dir = argv[2];
    struct { const char *name; void (*fn)(PImage*); } gens[] = {
        {"dark", gen_dark}, {"gradient", gen_gradient},
        {"flat", gen_flat}, {"mixed", gen_mixed}, {"solid", gen_solid},
    };
    for (size_t i = 0; i < sizeof(gens) / sizeof(gens[0]); ++i) {
        PImage im;
        gens[i].fn(&im);
        char path[512];
        snprintf(path, sizeof(path), "%s/%s.ppm", dir, gens[i].name);
        int rc = palette_save(path, &im);
        free(im.pix);
        if (rc != 0) { fprintf(stderr, "write failed: %s\n", path); return 7; }
        printf("gen %s %dx%d\n", path, im.w, im.h);
    }
    char manifest[512];
    snprintf(manifest, sizeof(manifest), "%s/manifest.txt", dir);
    FILE *f = fopen(manifest, "w");
    if (!f) return 7;
    fprintf(f, "# name path\n");
    for (size_t i = 0; i < 4; ++i)
        fprintf(f, "%s %s/%s.ppm\n", gens[i].name, dir, gens[i].name);
    fprintf(f, "solid %s/solid.ppm\n", dir);
    fclose(f);
    printf("gen %s\n", manifest);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage:\n"
                "  palette legacy <in> <out.ppm>\n"
                "  palette solve  <in> [k]\n"
                "  palette batch  <manifest> [k]\n"
                "  palette gen    <dir>\n");
        return 1;
    }
    if (!strcmp(argv[1], "legacy")) return cmd_legacy(argc, argv);
    if (!strcmp(argv[1], "solve")) return cmd_solve(argc, argv);
    if (!strcmp(argv[1], "batch")) return cmd_batch(argc, argv);
    if (!strcmp(argv[1], "gen")) return cmd_gen(argc, argv);
    fprintf(stderr, "unknown command: %s\n", argv[1]);
    return 1;
}
