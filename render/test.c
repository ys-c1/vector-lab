#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/fit.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int count_on_at(const RScene *s, RBox b, RPlace p, int n) {
    unsigned char *buf = (unsigned char*)malloc((size_t)n * n);
    int rc = pipeline_render(s, p, b, n, n, buf);
    int on = rc ? -1 : export_count_on(buf, n, n);
    free(buf);
    return on;
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

    /* ---- 导出字节数：头部 + 像素负载 ---- */
    CK(export_pgm_header_len(1, 1) == 11);   /* "P5\n1 1\n255\n" */
    CK(export_pgm_header_len(100, 50) == 14);
    CK(export_pgm_bytes(1, 1) == 12);        /* 11 头 + 1 像素 */
    CK(export_pgm_bytes(10, 10) == 113);     /* 13 头 + 100 */
    CK(export_pgm_header_len(0, 10) < 0);

    /* ---- 预算反解：方形场景 ---- */
    RFitResult r;
    /* 预算小于最小头部：明确报出，不渲染 */
    CK(fit_solve(&s, p, b, 32, 5, &r) == FIT_BUDGET_BELOW_HEADER);
    CK(r.header_min == 11 && r.bytes == 11 && r.size == 0);
    /* 预算只够头部、不够任何像素负载：无可行尺寸 */
    CK(fit_solve(&s, p, b, 32, 11, &r) == FIT_NO_FIT);
    CK(r.bytes == 12 && r.size == 0);
    /* 非法参数 */
    CK(fit_solve(&s, p, b, 0, 1000, &r) == FIT_BAD_ARG);
    CK(fit_solve(&s, p, b, 32, 0, &r) == FIT_BAD_ARG);

    /* 1x1 刚好放得下：可辨认（单像素即整个方块） */
    CK(fit_solve(&s, p, b, 32, 12, &r) == FIT_OK);
    CK(r.size == 1 && r.on == 1 && r.recognizable == 1);
    CK(r.bytes == 12 && r.bytes <= r.budget);
    CK(r.min_size == 1);

    /* 预算 300：二分得到的最大尺寸必须真的不超预算，且再大一号必超 */
    CK(fit_solve(&s, p, b, 32, 300, &r) == FIT_OK);
    CK(r.bytes <= 300);
    CK(export_pgm_bytes(r.size + 1, r.size + 1) > 300 || r.size == 32);
    CK(r.ref_on == count_on_at(&s, b, p, 32));

    /* 大预算：顶到 cap，字节仍不超预算 */
    CK(fit_solve(&s, p, b, 32, 100000, &r) == FIT_OK);
    CK(r.size == 32 && r.bytes == export_pgm_bytes(32, 32) && r.bytes <= 100000);

    /* 单调性：预算越大，选中尺寸不减小 */
    int prev = 0;
    long long budgets[] = {12, 20, 50, 100, 200, 500, 1000, 5000};
    for (unsigned i = 0; i < sizeof(budgets) / sizeof(budgets[0]); ++i) {
        CK(fit_solve(&s, p, b, 48, budgets[i], &r) == FIT_OK);
        CK(r.size >= prev);
        CK(r.bytes <= budgets[i]);
        prev = r.size;
    }

    /* ---- 微小三角形场景：压到图形消失必须拒绝 ---- */
    RScene tiny;
    scene_init(&tiny);
    CK(scene_add_poly(&tiny, sq, 4) == 0);
    RPoint tri[3] = {{0, 9.5}, {0.6, 9.5}, {0.3, 10}};
    CK(scene_add_poly(&tiny, tri, 3) == 0);
    RBox bt;
    CK(scene_bbox(&tiny, &bt) == 0);
    /* 满幅参考能同时看到方块和小三角 */
    int ref_on = count_on_at(&tiny, bt, p, 64);
    CK(ref_on > 0);
    /* 小预算：最大合规尺寸若不可辨认，必须拒绝并给出可辨认下限 */
    int st = fit_solve(&tiny, p, bt, 64, 200, &r);
    CK(st == FIT_OK || st == FIT_NOT_RECOGNIZABLE);
    if (st == FIT_NOT_RECOGNIZABLE) {
        CK(r.size == 0 && r.recognizable == 0 && r.min_size > 1);
        /* 报出的下限尺寸本身必须确实可辨认、且不超 cap */
        int on_min = count_on_at(&tiny, bt, p, r.min_size);
        CK(on_min > 0 && r.min_size <= 64);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
