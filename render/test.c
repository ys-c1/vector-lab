#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)
#define TPI 3.14159265358979323846

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
    /* 反解旋转角: 给四个包围盒角点各定期望转角, 反解后落点偏差须 <= 1 像素 */
    {
        RPlace pl = {0, 0, 0, 1.0};
        RBox wb = {0, 0, 10, 10};
        RPoint corner[4] = {{0,0},{10,0},{10,10},{0,10}};
        double want[4] = {0.5, 1.7, 3.5, -4.538}; /* -4.538 rad 归一化须抹 -1 圈 */
        RPoint tgt[4];
        RCorner so[4];
        for (int k = 0; k < 4; ++k) {
            double co = cos(want[k]), si = sin(want[k]);
            double ux = corner[k].x * co - corner[k].y * si;
            double uy = corner[k].x * si + corner[k].y * co;
            tgt[k].x = ux / 10.0 * 16 - 0.5;
            tgt[k].y = uy / 10.0 * 16 - 0.5;
        }
        CK(pipeline_solve_rot(&s, pl, wb, 16, 16, tgt, so) == 0);
        for (int k = 0; k < 4; ++k) {
            CK(so[k].dev <= 1.0);
            CK(so[k].angle > -TPI && so[k].angle <= TPI);
            CK(so[k].usec >= 0.0);
            /* 用反解角度前推, 落点与目标窗口坐标差须在一像素内 */
            double co = cos(so[k].angle), si = sin(so[k].angle);
            double lx = corner[k].x * co - corner[k].y * si;
            double ly = corner[k].x * si + corner[k].y * co;
            double ux = wb.minx + (tgt[k].x + 0.5) * (wb.maxx - wb.minx) / 16;
            double uy = wb.miny + (tgt[k].y + 0.5) * (wb.maxy - wb.miny) / 16;
            CK(fabs(lx - ux) <= (wb.maxx - wb.minx) / 16 + 1e-9);
            CK(fabs(ly - uy) <= (wb.maxy - wb.miny) / 16 + 1e-9);
        }
        CK(so[2].turns == 0); /* 3.5 rad 已在 (-pi, pi] 内, 无需抹圈 */
        CK(fabs(so[2].angle - (3.5 - 2 * TPI)) < 0.2);
        CK(so[3].turns == -1); /* 角点 (0,10) 期望 -4.538 rad, 归一化抹 -1 圈 */
        CK(fabs(so[3].angle - (-4.538 + 2 * TPI)) < 0.2);
        RPlace zsc = {0, 0, 0, 0.0};
        CK(pipeline_solve_rot(&s, zsc, wb, 16, 16, tgt, so) == -2);
        RBox empty = {0, 0, 0, 0};
        CK(pipeline_solve_rot(&s, pl, empty, 16, 16, tgt, so) == -3);
        CK(pipeline_solve_rot(&s, pl, wb, 0, 16, tgt, so) == -1);
    }
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
