#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/aspect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
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

    /* ---- aspect solve：反解窗口纵横比 ---- */
    RBox cbox = {10.0, 20.0, 30.0, 40.0};   /* bw=bh=20，中心 (20,30) */
    ASolve sol;
    /* 1:1 像素，窗口与包围盒一致 */
    CK(aspect_solve(&cbox, 400, 400, &sol) == 0);
    CK(fabs(sol.ratio - 1.0) < 1e-12 && fabs(sol.px_ratio - 1.0) < 1e-12);
    CK(fabs(sol.win.minx - 10.0) < 1e-12 && fabs(sol.win.maxx - 30.0) < 1e-12);
    CK(fabs(sol.win.miny - 20.0) < 1e-12 && fabs(sol.win.maxy - 40.0) < 1e-12);
    /* 2:1 像素：窗口宽高比须等于 2，居中且高度贴合 -> Ww=40, Wh=20 */
    CK(aspect_solve(&cbox, 600, 300, &sol) == 0);
    CK(fabs(sol.ratio - 2.0) < 1e-12);
    CK(fabs((sol.win.maxx - sol.win.minx) - 40.0) < 1e-12);
    CK(fabs((sol.win.maxy - sol.win.miny) - 20.0) < 1e-12);
    CK(fabs(sol.win.miny - 20.0) < 1e-12 && fabs(sol.win.maxy - 40.0) < 1e-12);
    CK(fabs(sol.win.minx - 0.0) < 1e-12 && fabs(sol.win.maxx - 40.0) < 1e-12);
    /* 1:2 像素：宽度贴合，Wh=40 */
    CK(aspect_solve(&cbox, 300, 600, &sol) == 0);
    CK(fabs(sol.ratio - 0.5) < 1e-12);
    CK(fabs((sol.win.maxx - sol.win.minx) - 20.0) < 1e-12);
    CK(fabs((sol.win.maxy - sol.win.miny) - 40.0) < 1e-12);
    /* 500x400：a=1.25，bw/bh=1 < a -> 宽度扩到 25，高贴合 */
    CK(aspect_solve(&cbox, 500, 400, &sol) == 0);
    CK(fabs(sol.ratio - 1.25) < 1e-12);
    CK(fabs((sol.win.maxx - sol.win.minx) - 25.0) < 1e-12);
    CK(fabs((sol.win.maxy - sol.win.miny) - 20.0) < 1e-12);
    /* 像素为零/负：拒，并指出边 */
    CK(aspect_solve(&cbox, 0, 400, &sol) == -2 && sol.bad_edge == ASPECT_EDGE_LR);
    CK(aspect_solve(&cbox, 400, -1, &sol) == -2 && sol.bad_edge == ASPECT_EDGE_BT);
    CK(aspect_solve(&cbox, 0, 0, &sol) == -2 && sol.bad_edge == ASPECT_EDGE_BOTH);
    /* 退化成点：单独报，四条边全重合 */
    RBox pbox = {7.0, 7.0, 7.0, 7.0};
    CK(aspect_solve(&pbox, 100, 100, &sol) == -3);
    CK(sol.degenerate == ASPECT_DEGEN_POINT && sol.bad_edge == ASPECT_EDGE_BOTH);
    /* 退化成线：左右边重合（竖线）/ 上下边重合（横线） */
    RBox vline = {5.0, 0.0, 5.0, 10.0};
    CK(aspect_solve(&vline, 100, 100, &sol) == -3);
    CK(sol.degenerate == ASPECT_DEGEN_LINE && sol.bad_edge == ASPECT_EDGE_LR);
    RBox hline = {0.0, 5.0, 10.0, 5.0};
    CK(aspect_solve(&hline, 100, 100, &sol) == -3);
    CK(sol.degenerate == ASPECT_DEGEN_LINE && sol.bad_edge == ASPECT_EDGE_BT);
    /* 反解后的窗口渲染圆：圆度偏差 <= 0.5% */
    RPoint ring[24];
    for (int i = 0; i < 24; ++i) {
        double a = 2.0 * M_PI * i / 24.0;
        ring[i].x = 50.0 + 20.0 * cos(a);
        ring[i].y = 50.0 + 20.0 * sin(a);
    }
    RScene cs;
    scene_init(&cs);
    CK(scene_add_poly(&cs, ring, 24) == 0);
    RBox cb;
    CK(scene_bbox(&cs, &cb) == 0);
    CK(aspect_solve(&cb, 600, 300, &sol) == 0);
    int cw = 120, ch = 60;
    unsigned char *cbuf = (unsigned char*)malloc((size_t)cw * ch);
    CK(cbuf != NULL);
    RPlace p2 = {0, 0, 0, 1.0};
    CK(pipeline_render(&cs, p2, sol.win, cw, ch, cbuf) == 0);
    double rx, ry, dev;
    CK(aspect_roundness(cbuf, cw, ch, &rx, &ry, &dev) == 0);
    CK(dev <= 0.005);
    free(cbuf);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
