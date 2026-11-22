#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/adapt.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int demo_probe(int ss, void *ctx, unsigned char *buf) {
    const AdaptRef *r = (const AdaptRef *)ctx;
    return raster_fill(r->scene, r->box, r->w, r->h, ss, buf);
}
static int inverted_probe(int ss, void *ctx, unsigned char *buf) {
    const AdaptRef *r = (const AdaptRef *)ctx;
    return raster_fill(r->scene, r->box, r->w, r->h, ADAPT_SS_MAX + 1 - ss, buf);
}
static RBox margin_box(const RBox *b, double m) {
    RBox o = { b->minx - m, b->miny - m, b->maxx + m, b->maxy + m };
    return o;
}

int main(void) {
    RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    CK(scene_count_verts(&s) == 4);
    RBox b;
    CK(scene_bbox(&s, &b) == 0);
    CK(b.minx == 0 && b.maxx == 10);
    unsigned char buf[16 * 16];
    CK(raster_fill(&s, &b, 16, 16, 4, buf) == 0);
    int on = export_count_on(buf, 16, 16);
    CK(on > 240 && on < 260);
    RPlace p = {0, 0, 0, 1.0};
    CK(pipeline_render(&s, p, b, 16, 16, buf) == 0);
    RPlace zero = {0, 0, 0, 0.0};
    CK(pipeline_render(&s, zero, b, 16, 16, buf) == -2);
    RBox bad = {0, 0, 0, 0};
    CK(pipeline_render(&s, p, bad, 16, 16, buf) == -3);
    CK(scene_add_poly(&s, sq, 2) == -1);

    /* ---- ss 为 0 或负、超过 8：raster_fill 与 adapt 都必须拒绝 ---- */
    CK(raster_fill(&s, &b, 16, 16, 0, buf) == -1);
    CK(raster_fill(&s, &b, 16, 16, -1, buf) == -1);
    CK(raster_fill(&s, &b, 16, 16, 9, buf) == -1);

    /* ---- 自适应：斜边场景 tol=0.10 应反解出 ss=5 ---- */
    RPoint dia[4] = {{10,10},{50,18},{42,52},{2,44}};
    RScene ds; scene_init(&ds);
    CK(scene_add_poly(&ds, dia, 4) == 0);
    RBox db; scene_bbox(&ds, &db);
    RBox dw = margin_box(&db, 4.0);
    int W = 64, H = 64;
    int *cov = malloc(sizeof(int) * W * H);
    unsigned char *abuf = malloc((size_t)W * H);
    AdaptRef ref = {&ds, &dw, W, H, cov};
    CK(adapt_ref_build(&ref) == ADAPT_OK);
    AdaptResult info;
    AdaptTarget t = { demo_probe, &ref, &ref, 0.10, 1, 8, abuf };
    CK(adapt_solve(&t, &info) == ADAPT_OK);
    CK(info.ss == 5);
    CK(info.metric <= 0.10);
    CK(info.nprobes >= 1 && info.nprobes <= 4);
    /* 所有被探测的指标必须落在 [0,1] */
    for (int ss = 1; ss <= 8; ++ss)
        if (info.probe_ms[ss] >= 0.0)
            CK(info.metric_at[ss] >= 0.0 && info.metric_at[ss] <= 1.0);

    /* 参数守卫：区间含 0、负值、上界超 8、下界>上界、阈值非法 */
    AdaptTarget tbad = { demo_probe, &ref, &ref, 0.10, 0, 8, abuf };
    CK(adapt_solve(&tbad, &info) == ADAPT_ERR_ARG);
    tbad.ss_lo = -2;
    CK(adapt_solve(&tbad, &info) == ADAPT_ERR_ARG);
    tbad.ss_lo = 1; tbad.ss_hi = 9;
    CK(adapt_solve(&tbad, &info) == ADAPT_ERR_ARG);
    tbad.ss_hi = 3; tbad.ss_lo = 5;
    CK(adapt_solve(&tbad, &info) == ADAPT_ERR_ARG);
    tbad.ss_lo = 1; tbad.ss_hi = 8; tbad.tol = 0.0;
    CK(adapt_solve(&tbad, &info) == ADAPT_ERR_ARG);
    tbad.tol = 1.5;
    CK(adapt_solve(&tbad, &info) == ADAPT_ERR_ARG);

    /* ---- NOPASS：tol=0.01 尖角场景 ss=8 仍不达标 ---- */
    RPoint tri2[3] = {{4,60},{7.2,4},{60,32}};
    RScene sh; scene_init(&sh);
    CK(scene_add_poly(&sh, tri2, 3) == 0);
    RBox sb; scene_bbox(&sh, &sb);
    RBox sw = margin_box(&sb, 4.0);
    int *cov2 = malloc(sizeof(int) * W * H);
    AdaptRef ref2 = {&sh, &sw, W, H, cov2};
    CK(adapt_ref_build(&ref2) == ADAPT_OK);
    AdaptResult hr;
    AdaptTarget hard = { demo_probe, &ref2, &ref2, 0.01, 1, 8, abuf };
    CK(adapt_solve(&hard, &hr) == ADAPT_ERR_NOPASS);
    CK(hr.ss == 8 && hr.metric > 0.01);

    /* ---- NONMONO：反向探针制造指标上升，必须拒绝并给出双方数值 ---- */
    AdaptResult nr;
    AdaptTarget inv = { inverted_probe, &ref2, &ref2, 0.10, 1, 8, abuf };
    CK(adapt_solve(&inv, &nr) == ADAPT_ERR_NONMONO);
    CK(nr.mono_b > nr.mono_a);
    CK(nr.metric_b > nr.metric_a);

    /* ---- 关闭新逻辑：pipeline_render_opt(adaptive=0) 与老路径逐字节相同 ---- */
    unsigned char oldb[256 * 256], newb[256 * 256];
    RPoint big[4] = {{20.375,20.375},{120.375,20.375},{120.375,120.375},{20.375,120.375}};
    RScene bs; scene_init(&bs);
    CK(scene_add_poly(&bs, big, 4) == 0);
    RBox bb2; scene_bbox(&bs, &bb2);
    RBox bw = margin_box(&bb2, 16.0);
    CK(pipeline_render(&bs, p, bw, 256, 256, oldb) == 0);
    CK(pipeline_render_opt(&bs, p, bw, 256, 256, 0, 0.10, newb, NULL) == 0);
    CK(memcmp(oldb, newb, sizeof oldb) == 0);
    /* 开自适应的接口也能跑通且 ss 在 [1,8] */
    AdaptResult oi;
    CK(pipeline_render_opt(&bs, p, bw, 256, 256, 1, 0.10, newb, &oi) == 0);
    CK(oi.ss >= 1 && oi.ss <= 8);
    CK(pipeline_render_opt(&bs, p, bw, 256, 256, 1, 0.0, newb, &oi) == -5);

    free(cov); free(cov2); free(abuf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
