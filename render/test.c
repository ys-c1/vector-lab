#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/invsolve.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
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

    /* ---- 反解缩放 ---- */
    unsigned char *ibuf = (unsigned char*)malloc(32 * 32);
    unsigned char *fbuf = (unsigned char*)malloc(48 * 48);
    CK(ibuf && fbuf);
    InvResult ir;
    /* 正常图形：反解成功、边缘零丢失、实际密度达标、缩放不越上界 */
    CK(inverse_solve(&s, &b, 32, 32, 8.0, 0.0, &ir, ibuf, fbuf) == 0);
    CK(ir.edge_lost == 0);
    CK(ir.density >= 8.0);
    CK(ir.scale > 0.0 && ir.scale <= ir.scale_max);
    CK(ir.degenerate == 0);
    /* 最小性：再小一档（二分 lo）密度必然不足 */
    RScene chk;
    RPoly tmp = s.polys[0];
    for (int i = 0; i < tmp.n; ++i) { tmp.pts[i].x *= ir.scale * 0.999; tmp.pts[i].y *= ir.scale * 0.999; }
    scene_init(&chk);
    CK(scene_add_poly(&chk, tmp.pts, tmp.n) == 0);
    CK(raster_fill(&chk, &b, 32, 32, 4, ibuf) == 0);
    CK((double)export_count_on(ibuf, 32, 32) / 100.0 < 8.0);
    /* 目标密度越界：拒绝并给上限 */
    CK(inverse_solve(&s, &b, 32, 32, 1.0e6, 0.0, &ir, ibuf, fbuf) == -8);
    CK(ir.scale_max > 0.0);
    /* 外部缩放上限同样生效 */
    CK(inverse_solve(&s, &b, 32, 32, 1.0e6, 0.5, &ir, ibuf, fbuf) == -8);
    CK(ir.scale_max == 0.5);
    /* 退化图形：总面积为零必须拒绝并报告 */
    RScene deg;
    RPoint flat[3] = {{0,0},{10,0},{20,0}};
    scene_init(&deg);
    CK(scene_add_poly(&deg, flat, 3) == 0);
    RBox db; CK(scene_bbox(&deg, &db) == 0);
    CK(inverse_solve(&deg, &db, 16, 16, 8.0, 0.0, &ir, ibuf, fbuf) == -7);
    CK(ir.degenerate == 1 && ir.degenerate_polys == 1);
    /* 含退化多边形但总面积非零：继续反解并标记（退化多边形为重复顶点，不压垮包围盒） */
    RScene mix;
    RPoint dup[3] = {{2,2},{5,5},{2,2}};
    scene_init(&mix);
    CK(scene_add_poly(&mix, sq, 4) == 0);
    CK(scene_add_poly(&mix, dup, 3) == 0);
    RBox mb; CK(scene_bbox(&mix, &mb) == 0);
    CK(inverse_solve(&mix, &mb, 32, 32, 8.0, 0.0, &ir, ibuf, fbuf) == 0);
    CK(ir.degenerate == 1 && ir.degenerate_polys == 1 && ir.edge_lost == 0);
    /* 边缘验证器本身：1.5 倍放大超出窗口必须抓到丢失，1.0 倍不丢 */
    RScene over;
    RPoly op = s.polys[0];
    for (int i = 0; i < op.n; ++i) { op.pts[i].x *= 1.5; op.pts[i].y *= 1.5; }
    scene_init(&over);
    CK(scene_add_poly(&over, op.pts, op.n) == 0);
    CK(inverse_edge_lost(&over, &b, 32, 32, fbuf, 48 * 48) > 0);
    RScene fit;
    RPoly fp2 = s.polys[0];
    for (int i = 0; i < fp2.n; ++i) { fp2.pts[i].x *= 0.5; fp2.pts[i].y *= 0.5; }
    scene_init(&fit);
    CK(scene_add_poly(&fit, fp2.pts, fp2.n) == 0);
    CK(inverse_edge_lost(&fit, &b, 32, 32, fbuf, 48 * 48) == 0);
    /* 缓冲容量不足时返回 -1 而非越界写 */
    CK(inverse_edge_lost(&over, &b, 32, 32, fbuf, 4) == -1);
    /* 非法参数 */
    CK(inverse_solve(&s, &b, 32, 32, 0.0, 0.0, &ir, ibuf, fbuf) == -1);
    free(ibuf); free(fbuf);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
