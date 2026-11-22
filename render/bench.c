#define _POSIX_C_SOURCE 200809L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    scene_init(s);
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
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -2; }
    }
    fclose(f);
    return 0;
}

/* bench <scene.txt> <b0,b1,...> [--ref N]
 * 每个预算独立跑一次完整分配（plan+pick），再渲染一次。
 * 输出 TSV：
 *   budget  w  h  ss  cost  jag  alloc_ms  render_ms  monotone
 * alloc_ms = 反解 (w,h,ss) 的耗时；render_ms = 按该搭配出图的耗时。
 * 指标必须随预算单调不升，违反时退出码 6。 */
int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s scene.txt b0,b1,... [--ref N]\n", argv[0]);
        return 1;
    }
    const char *scene_path = argv[1];
    const char *tiers = argv[2];
    int ref_side = 0;
    for (int i = 3; i < argc; ++i)
        if (strcmp(argv[i], "--ref") == 0 && i + 1 < argc) ref_side = atoi(argv[++i]);

    RScene s;
    if (load_scene(scene_path, &s) != 0) { fprintf(stderr, "load scene failed\n"); return 2; }
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 2;

    long budgets[64];
    int nb = 0;
    const char *p = tiers;
    while (*p && nb < 64) {
        budgets[nb++] = atol(p);
        const char *comma = strchr(p, ',');
        if (!comma) break;
        p = comma + 1;
    }

    printf("scene\t%s\n", scene_path);
    printf("budget\tw\th\tss\tcost\tjag\talloc_ms\trender_ms\tmonotone\n");

    double prev = -1.0;
    int mono = 1;
    int have = 0;
    for (int i = 0; i < nb; ++i) {
        long B = budgets[i];
        if (B <= 0) {
            printf("%ld\t-\t-\t-\t-\tBUDGET_ZERO\t0.0\t0.0\t%s\n", B,
                   mono ? "yes" : "NO");
            continue;
        }
        double t0 = now_ms();
        RAlloc a;
        RAllocReport rep;
        int rc = ralloc_solve(&s, NULL, B, ref_side, &a, &rep);
        double alloc_ms = now_ms() - t0;
        if (rc == RALLOC_NO_FIT) {
            printf("%ld\t-\t-\t-\t-\tNO_FIT\t%.1f\t0.0\t%s\n", B, alloc_ms,
                   mono ? "yes" : "NO");
            continue;
        }
        if (rc != RALLOC_OK) {
            printf("%ld\t-\t-\t-\t-\tERR_%d\t%.1f\t0.0\t%s\n", B, rc, alloc_ms,
                   mono ? "yes" : "NO");
            continue;
        }
        unsigned char *buf = (unsigned char *)malloc((size_t)a.w * a.h);
        if (!buf) return 4;
        double t1 = now_ms();
        if (pipeline_render_ss(&s, (RPlace){0, 0, 0, 1.0}, win, a.w, a.h, a.ss, buf) != 0) {
            free(buf);
            return 5;
        }
        double render_ms = now_ms() - t1;
        free(buf);
        if (have && a.jag > prev + 1e-12) mono = 0;
        prev = a.jag;
        have = 1;
        printf("%ld\t%d\t%d\t%d\t%ld\t%.6f\t%.1f\t%.1f\t%s\n",
               B, a.w, a.h, a.ss, a.cost, a.jag, alloc_ms, render_ms,
               mono ? "yes" : "NO");
    }
    return mono ? 0 : 6;
}
