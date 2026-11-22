#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/cull.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

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

    /* ---- cull：期望代价阈值 ---- */
    double szs[4] = {0.5, 0.6, 1.5, 1.6};
    double cvs[4] = {0, 0, 3, 4};
    double T = 0, cost = 0;
    CK(cull_pick_threshold(szs, cvs, 4, 0.5, &T, &cost) == 0);
    CK(cost == 0.0);                 /* 间隙存在：剔掉两个零覆盖，期望缺块为 0 */
    CK(T > 0.6 && T < 1.5);
    CK(cull_pick_threshold(szs, cvs, 4, -0.5, &T, &cost) == -1); /* 负权重拒绝 */
    CK(cull_pick_threshold(szs, cvs, 0, 0.5, &T, &cost) == -2);  /* 退化：空 */

    /* 关掉新逻辑逐字节不变；AUTO 只剔零覆盖，与参照逐像素一致 */
    RScene s2;
    scene_init(&s2);
    RPoint vis[4] = {{2,2},{14,2},{14,14},{2,14}};
    RPoint off[4] = {{50,50},{60,50},{60,60},{50,60}};
    RPoint slv[4] = {{3.95,2},{4.00,2},{4.00,14},{3.95,14}};
    CK(scene_add_poly(&s2, vis, 4) == 0);
    CK(scene_add_poly(&s2, off, 4) == 0);
    CK(scene_add_poly(&s2, slv, 4) == 0);
    RBox w2 = {0, 0, 16, 16};
    unsigned char r1[16 * 16], r2[16 * 16];
    CK(pipeline_render(&s2, p, w2, 16, 16, r1) == 0);
    RCullCfg coff = {RCULL_OFF, 2.0, 0.5};
    CK(pipeline_render_ex(&s2, p, w2, 16, 16, &coff, NULL, r2) == 0);
    CK(memcmp(r1, r2, sizeof r1) == 0);
    RCullCfg cauto = {RCULL_AUTO, 0.0, 0.5};
    RCullStats cst;
    CK(pipeline_render_ex(&s2, p, w2, 16, 16, &cauto, &cst, r2) == 0);
    CK(memcmp(r1, r2, sizeof r1) == 0);
    CK(cst.free_culls == 1 && cst.thresh_culls == 1 && cst.kept == 1);
    CK(cst.expected == 0.0);
    RCullCfg cbad = {RCULL_AUTO, 0.0, -1.0};
    CK(pipeline_render_ex(&s2, p, w2, 16, 16, &cbad, NULL, r2) == -5);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
