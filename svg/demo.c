#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>

#if SVG_REL

#include <math.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* 相对/绝对混写：l/h/v 按当前笔位累加 */
static const char *D_MIXED =
    "M5,5 l40,0 v40 h-40 Z "
    "M15,15 L35,15 l0,20 h-20 Z";
/* 手写绝对版本，与上面描述同一图形，用于逐点比对 */
static const char *D_ABS =
    "M5,5 L45,5 L45,45 L5,45 Z "
    "M15,15 L35,15 L35,35 L15,35 Z";

#define REPS 2000

int main(void) {
    VPath pm, pa;
    int es = 0;
    if (svg_parse_ex(D_MIXED, &pm, &es) != 0) { printf("mixed parse err at seg %d\n", es); return 1; }
    if (svg_parse_ex(D_ABS, &pa, &es) != 0) { printf("abs parse err at seg %d\n", es); return 1; }
    if (pm.count != pa.count) { printf("seg count mismatch\n"); return 1; }

    /* 统一端点表：相对段与绝对段都归约为同一张 endpoints[]，逐点比对 */
    VPoint ep_m[256], ep_a[256];
    double *ns = (double *)calloc((size_t)pm.count, sizeof(double));
    if (!ns) return 1;
    for (int r = 0; r < REPS; ++r)
        for (int i = 0; i < pm.count; ++i) {
            double t0 = now_s();
            ep_m[i] = pm.segs[i].b;
            ep_a[i] = pa.segs[i].b;
            ns[i] += (now_s() - t0) * 1e9;
        }

    double dev = 0.0;
    for (int i = 0; i < pm.count; ++i) {
        double d = fabs(ep_m[i].x - ep_a[i].x);
        if (d > dev) dev = d;
        d = fabs(ep_m[i].y - ep_a[i].y);
        if (d > dev) dev = d;
    }

    printf("段号  端点                耗时(ns)\n");
    for (int i = 0; i < pm.count; ++i)
        printf("%-5d (%7.3f,%7.3f)  %8.1f\n",
               i + 1, ep_m[i].x, ep_m[i].y, ns[i] / REPS);
    printf("端点偏差上界: %.3e\n", dev);
    free(ns);

    /* 两档图幅：各报端点数和填充耗时 */
    static const int SIZES[2] = {50, 200};
    printf("图幅        端点数  耗时(ms)\n");
    for (int k = 0; k < 2; ++k) {
        int w = SIZES[k], h = SIZES[k];
        unsigned char *buf = (unsigned char *)malloc((size_t)w * h);
        if (!buf) return 1;
        double t0 = now_s();
        if (svg_fill(&pm, w, h, buf) != 0) return 1;
        double ms = (now_s() - t0) * 1e3;
        int on = 0;
        for (int i = 0; i < w * h; ++i) if (buf[i]) ++on;
        printf("%-3dx%-6d %-6d  %8.3f\n", w, h, pm.count, ms);
        if (k == 0 && svg_save_pgm("demo.pgm", buf, w, h) != 0) return 1;
        (void)on;
        free(buf);
    }
    return 0;
}

#else /* !SVG_REL：初版 demo，原样保留 */

int main(void) {
    VPath p;
    if (svg_parse("M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z", &p) != 0) return 1;
    int w = 50, h = 50;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (svg_fill(&p, w, h, buf) != 0) return 1;
    int on = 0;
    for (int i = 0; i < w * h; ++i) if (buf[i]) ++on;
    if (svg_save_pgm("demo.pgm", buf, w, h) != 0) return 1;
    printf("segs %d on %d\n", p.count, on);
    free(buf);
    return 0;
}

#endif /* SVG_REL */
