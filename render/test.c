#define _POSIX_C_SOURCE 200809L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/alloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static RScene make_scene(void) {
    RScene s;
    scene_init(&s);
    RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    scene_add_poly(&s, sq, 4);
    RPoint tri[3] = {{13,1},{24,7},{14,10}};
    scene_add_poly(&s, tri, 3);
    return s;
}

int main(void) {
    /* ---- 旧链路 ---- */
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

    /* ---- 覆盖率渲染：灰阶单调、边界介于 0/255 之间 ---- */
    RScene sc = make_scene();
    RBox wb;
    CK(scene_bbox(&sc, &wb) == 0);
    unsigned char cov[32 * 32];
    CK(raster_coverage(&sc, &wb, 32, 32, 1, cov) == 0);
    int mid1 = 0;
    for (int i = 0; i < 32 * 32; ++i) if (cov[i] != 0 && cov[i] != 255) ++mid1;
    CK(mid1 == 0);                    /* ss=1 每像素只有 0/255 两档 */
    unsigned char cov8[32 * 32];
    CK(raster_coverage(&sc, &wb, 32, 32, 8, cov8) == 0);
    int mid8 = 0;
    for (int i = 0; i < 32 * 32; ++i) if (cov8[i] != 0 && cov8[i] != 255) ++mid8;
    CK(mid8 > 0);                     /* 超采样产生中间灰阶 */
    CK(raster_coverage(&sc, &wb, 32, 32, 0, cov8) == -1);
    CK(raster_coverage(&sc, &wb, 32, 32, 9, cov8) == -1);
    CK(pipeline_render_ss(&sc, p, wb, 32, 32, 3, cov8) == 0);
    CK(pipeline_render_ss(&sc, p, wb, 32, 32, 9, cov8) == -6);

    /* ---- 分配：四档预算、单调、尺寸/ss 都在界内 ---- */
    long tiers[4] = {4096, 16384, 65536, 262144};
    double prev = -1.0;
    for (int t = 0; t < 4; ++t) {
        RAlloc a;
        RAllocReport rep;
        CK(ralloc_solve(&sc, NULL, tiers[t], 0, &a, &rep) == RALLOC_OK);
        CK(a.cost <= tiers[t]);
        CK(a.w >= 1 && a.h >= 1);
        CK(a.ss >= 1 && a.ss <= RALLOC_MAX_SS);
        CK(a.cost == (long)a.w * a.h * a.ss * a.ss);
        CK(a.jag >= 0.0);
        if (prev >= 0.0) CK(a.jag <= prev + 1e-12);
        prev = a.jag;
    }

    /* ---- 预算为零要报出来 ---- */
    RAlloc z;
    CK(ralloc_solve(&sc, NULL, 0, 0, &z, NULL) == RALLOC_BUDGET_ZERO);
    CK(ralloc_solve(&sc, NULL, -5, 0, &z, NULL) == RALLOC_BUDGET_ZERO);

    /* ---- 极小预算不给虚假成功 ---- */
    RAlloc tiny;
    int tr = ralloc_solve(&sc, NULL, 1, 0, &tiny, NULL);
    CK(tr == RALLOC_OK || tr == RALLOC_NO_FIT);
    if (tr == RALLOC_OK) CK(tiny.cost <= 1);

    /* ---- 越界检查：分类并说明超哪一项 ---- */
    char msg[256];
    CK(ralloc_check(100, 100, 1, 5000, msg, sizeof(msg)) == RALLOC_OVER_SIZE);
    CK(strstr(msg, "size over") != NULL);
    CK(ralloc_check(50, 50, 4, 5000, msg, sizeof(msg)) == RALLOC_OVER_SS);
    CK(strstr(msg, "ss over") != NULL);
    CK(ralloc_check(10, 10, 0, 5000, msg, sizeof(msg)) == RALLOC_OVER_RANGE);
    CK(ralloc_check(10, 10, 9, 5000, msg, sizeof(msg)) == RALLOC_OVER_RANGE);
    CK(ralloc_check(100, 100, 9, 5000, msg, sizeof(msg)) == RALLOC_OVER_BOTH);
    CK(ralloc_check(10, 10, 2, 5000, msg, sizeof(msg)) == 0);

    /* ---- 越界拒绝必须与代价模型一致 ---- */
    for (long B = 100; B <= 3000; B += 137) {
        for (int w = 1; w <= 20; w += 3)
            for (int h = 1; h <= 20; h += 3)
                for (int ss = 1; ss <= 8; ++ss) {
                    int rc = ralloc_check(w, h, ss, B, msg, sizeof(msg));
                    long cost = (long)w * h * ss * ss;
                    CK((rc == 0) == (cost <= B));
                }
    }

    /* ---- 候选集里最优 J 单调：用同一 plan 逐个预算 pick ---- */
    RAllocPlan plan;
    RAllocReport rep;
    CK(ralloc_plan(&sc, NULL, 0, 262144, &plan, &rep) == RALLOC_OK);
    CK(plan.n > 0);
    double last = -1.0;
    for (long B = 1; B <= 300000; B = B * 3 / 2 + 7) {
        RAlloc a;
        int rc = ralloc_pick(&plan, B, &a);
        if (rc == RALLOC_OK) {
            if (last >= 0.0) CK(a.jag <= last + 1e-12);
            last = a.jag;
            CK(a.cost <= B);
        }
    }
    ralloc_plan_free(&plan);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
