#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/snap.h"
#include <math.h>
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

    /* ---- snap：反解、预算、拒收、退化单列 ---- */
    RPoint q[4] = {{0.37,0.41},{8.83,0.62},{9.21,6.94},{0.54,7.31}};
    RScene a;
    scene_init(&a);
    CK(scene_add_poly(&a, q, 4) == 0);
    double px = 0.0920833333333333, py = 0.071875;
    SnapSolve sol;
    RScene sn;
    CK(snap_solve(&a, px, py, 0.37, 0.41, 1e-3, 64, &sol, &sn) == 0);
    CK(sol.rc == 0);
    CK(sol.chosen.err <= 1e-3);
    CK(sol.chosen.worst_err <= 1e-3);
    CK(fabs(sol.chosen.hx - px * sol.chosen.m * (sol.chosen.coarse ? 1 : 0.0)
             - (sol.chosen.coarse ? 0.0 : px / sol.chosen.m)) < 1e-15);
    /* 最粗可行：粗一档（若同为 pitch*m）要么塌陷要么超预算 */
    if (sol.chosen.coarse && sol.chosen.m < 64) {
        SnapEval up;
        RScene tmp;
        int rc = snap_eval(&a, px, py, 0.37, 0.41, 1, sol.chosen.m + 1, &up, &tmp);
        CK(rc == 0);
        CK(up.first_bad >= 0 || up.err > 1e-3 || up.worst_err > 1e-3);
    }
    /* 吸附落在格点上 */
    for (int i = 0; i < sn.polys[0].n; ++i) {
        double gx = (sn.polys[0].pts[i].x - 0.37) / sol.chosen.hx;
        double gy = (sn.polys[0].pts[i].y - 0.41) / sol.chosen.hy;
        CK(fabs(gx - (double)llround(gx)) < 1e-9);
        CK(fabs(gy - (double)llround(gy)) < 1e-9);
    }
    /* 塌陷拒收：细长的三角形在粗步长下退化成线段 */
    RPoint sl[3] = {{0,0},{10,0.02},{10,0}};
    RScene thin;
    scene_init(&thin);
    CK(scene_add_poly(&thin, sl, 3) == 0);
    SnapEval bad_ev;
    RScene bt;
    CK(snap_eval(&thin, 9.0, 9.0, 0.0, 0.0, 1, 1, &bad_ev, &bt) == 0);
    CK(bad_ev.first_bad == 0);
    CK(bad_ev.status[0] == SNAP_DEG_SEGMENT || bad_ev.status[0] == SNAP_DEG_POINT);

    /* 退化输入（点）单独分类、不进误差分母 */
    RPoint pt[3] = {{3,3},{3,3},{3,3}};
    RPoint sqb[4] = {{0,0},{10,0},{10,10},{0,10}};
    RScene mix;
    scene_init(&mix);
    CK(scene_add_poly(&mix, sqb, 4) == 0);
    CK(scene_add_poly(&mix, pt, 3) == 0);
    SnapSolve msol;
    RScene msn;
    CK(snap_solve(&mix, 0.15625, 0.15625, 0.0, 0.0, 1e-3, 64, &msol, &msn) == 0);
    CK(msol.chosen.status[1] == SNAP_INPUT_DEGEN);
    CK(msol.chosen.input_kind[1] == 1);
    CK(fabs(msol.chosen.area0 - 100.0) < 1e-9);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
