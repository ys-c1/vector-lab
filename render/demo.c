/*
 * 自适应超采样演示：
 *   1. 直边/斜边/细缝/尖角四组场景，二分反解达标最小 ss，
 *      报告 ss、边缘锯齿指标、逐探针与总耗时；
 *   2. ss=0/-1/9 与非法区间必须报错；
 *   3. 构造“指标随 ss 上升”的探针，求解器必须拒绝；
 *   4. 新逻辑关闭时与老的固定 ss=4 路径逐字节一致。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/adapt.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 256
#define H 256
#define TOL 0.10

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static RBox margin_box(const RBox *b, double m) {
    RBox o = { b->minx - m, b->miny - m, b->maxx + m, b->maxy + m };
    return o;
}

typedef struct {
    const char *name;
    RPoint pts[2][64];
    int n[2], npoly;
} CaseDef;

static void build_scene(const CaseDef *d, RScene *s) {
    scene_init(s);
    for (int k = 0; k < d->npoly; ++k)
        scene_add_poly(s, d->pts[k], d->n[k]);
}

static void run_case(const CaseDef *d) {
    RScene s;
    build_scene(d, &s);
    RBox bb;
    scene_bbox(&s, &bb);
    RBox win = margin_box(&bb, 16.0);
    unsigned char *buf = malloc((size_t)W * H);

    double t0 = now_ms();
    AdaptResult info;
    int rc = pipeline_render_opt(&s, (RPlace){0,0,0,1.0}, win, W, H,
                                 1, TOL, buf, &info);
    double total = now_ms() - t0;

    printf("[%s] rc=%d\n", d->name, rc);
    if (rc == ADAPT_OK) {
        printf("  -> 最小 ss=%d  锯齿指标=%.4f (%d/%d 边缘像素不一致)  探针 %d 次  总耗时 %.2f ms\n",
               info.ss, info.metric, info.bad_pixels, info.edge_pixels,
               info.nprobes, total);
        printf("     逐探针(只改 ss，场景/窗口/尺寸不动):\n");
        for (int ss = ADAPT_SS_MIN; ss <= ADAPT_SS_MAX; ++ss)
            if (info.probe_ms[ss] >= 0.0)
                printf("       ss=%d  metric=%.4f  耗时 %.2f ms\n",
                       ss, info.metric_at[ss], info.probe_ms[ss]);
    } else {
        printf("  -> %s\n", adapt_strerror(rc));
    }
    free(buf);
}

/* 故意反向：ss 越大，底层反而用越小的采样，制造指标上升。 */
static int inverted_probe(int ss, void *ctx, unsigned char *buf) {
    const AdaptRef *r = (const AdaptRef *)ctx;
    return raster_fill(r->scene, r->box, r->w, r->h, ADAPT_SS_MAX + 1 - ss, buf);
}

int main(void) {
    CaseDef cases[4] = {
        { "straight 直边", {{{20.375,20.375},{120.375,20.375},{120.375,120.375},{20.375,120.375}}},
          {4}, 1 },
        { "diagonal 斜边", {{{40,40},{200,72},{168,208},{8,176}}},
          {4}, 1 },
        { "slit 细缝(宽2.4)", {
            {{20,32},{126.8,32},{126.8,224},{20,224}},
            {{129.2,32},{236,32},{236,224},{129.2,224}} },
          {4,4}, 2 },
        { "sharp 尖角", {{{16,240},{32,16},{240,128}}},
          {3}, 1 },
    };
    printf("== 四组场景：tol=%.2f，画布 %dx%d，ss 合法域 [1,8] ==\n", TOL, W, H);
    for (int i = 0; i < 4; ++i) run_case(&cases[i]);

    /* ---- 参数守卫：ss 为 0 或负、超过 8 必须报出来 ---- */
    printf("\n== 参数守卫 ==\n");
    RScene s;
    build_scene(&cases[0], &s);
    RBox bb; scene_bbox(&s, &bb);
    RBox win = margin_box(&bb, 16.0);
    unsigned char *buf = malloc((size_t)W * H);
    int bad_ss[3] = {0, -1, 9};
    for (int i = 0; i < 3; ++i) {
        int rc = raster_fill(&s, &win, W, H, bad_ss[i], buf);
        printf("  raster_fill(ss=%d) -> rc=%d %s\n",
               bad_ss[i], rc, rc == -1 ? "(已拒绝)" : "(未拒绝!)");
    }
    int *cov = malloc(sizeof(int) * W * H);
    AdaptRef ref = {&s, &win, W, H, cov};
    adapt_ref_build(&ref);
    AdaptTarget t0bad = { NULL, NULL, &ref, TOL, 0, 8, buf };
    printf("  adapt_solve(ss_lo=0) -> rc=%d (%s)\n",
           adapt_solve(&t0bad, &(AdaptResult){0}), adapt_strerror(ADAPT_ERR_ARG));
    free(cov);

    /* ---- 单调性守卫：指标不降反升必须拒绝并说明 ---- */
    printf("\n== 单调性守卫（反向探针，指标应随 ss 上升） ==\n");
    RPoint tri2[3] = {{4,60},{7.2,4},{60,32}};
    RScene sharp2;
    scene_init(&sharp2);
    scene_add_poly(&sharp2, tri2, 3);
    RBox sb; scene_bbox(&sharp2, &sb);
    RBox swin = margin_box(&sb, 4.0);
    int SW = 64, SH = 64;
    int *cov2 = malloc(sizeof(int) * SW * SH);
    AdaptRef ref2 = {&sharp2, &swin, SW, SH, cov2};
    adapt_ref_build(&ref2);
    unsigned char *sbuf = malloc((size_t)SW * SH);
    AdaptResult rr;
    AdaptTarget inv = { inverted_probe, &ref2, &ref2, 0.01, 1, 8, sbuf };
    int rc = adapt_solve(&inv, &rr);
    printf("  rc=%d\n", rc);
    if (rc == ADAPT_ERR_NONMONO)
        printf("  -> 已拒绝：ss=%d 指标 %.4f 高于更小的 ss=%d 指标 %.4f（锯齿不降反升）\n",
               rr.mono_b, rr.metric_b, rr.mono_a, rr.metric_a);

    /* ---- 达标守卫：ss=8 仍不达标 ---- */
    AdaptTarget hard = { NULL, NULL, &ref2, 0.01, 1, 8, sbuf };
    /* 用正常探针：直接借助 pipeline 内部同规则的 raster_fill */
    AdaptResult hr;
    /* Target 需要探针，下面用局部包装 */
    extern int normal_probe_demo(int, void *, unsigned char *);
    hard.probe = normal_probe_demo; hard.ctx = &ref2;
    rc = adapt_solve(&hard, &hr);
    printf("  tol=0.01 正常探针 -> rc=%d, ss=%d 指标 %.4f (%s)\n",
           rc, hr.ss, hr.metric,
           rc == ADAPT_ERR_NOPASS ? "ss=8 仍不达标，已报 NOPASS" : adapt_strerror(rc));
    free(cov2); free(sbuf);

    /* ---- 老路径逐字节不变 ---- */
    printf("\n== 关闭新逻辑：与固定 ss=4 老路径逐字节比对 ==\n");
    unsigned char *oldb = malloc((size_t)W * H), *newb = malloc((size_t)W * H);
    for (int i = 0; i < 4; ++i) {
        RScene cs; build_scene(&cases[i], &cs);
        RBox cb; scene_bbox(&cs, &cb);
        RBox cw = margin_box(&cb, 16.0);
        pipeline_render(&cs, (RPlace){0,0,0,1.0}, cw, W, H, oldb);
        AdaptResult info;
        pipeline_render_opt(&cs, (RPlace){0,0,0,1.0}, cw, W, H, 0, TOL, newb, &info);
        int same = memcmp(oldb, newb, (size_t)W * H) == 0;
        char p1[64], p2[64];
        snprintf(p1, sizeof p1, "build/old_%d.pgm", i);
        snprintf(p2, sizeof p2, "build/new_%d.pgm", i);
        export_pgm(p1, oldb, W, H);
        export_pgm(p2, newb, W, H);
        printf("  %-16s memcmp=%s  PGM: %s %s\n", cases[i].name,
               same ? "IDENTICAL" : "DIFF!!", p1, p2);
    }
    free(oldb); free(newb); free(buf);
    return 0;
}

/* 正常探针（raster_fill 签名与 AdaptProbeFn 相容，ctx 取 AdaptRef）。 */
int normal_probe_demo(int ss, void *ctx, unsigned char *buf) {
    const AdaptRef *r = (const AdaptRef *)ctx;
    return raster_fill(r->scene, r->box, r->w, r->h, ss, buf);
}
