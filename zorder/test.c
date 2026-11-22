#include "src/zorder.h"
#include "../render/src/scene.h"
#include "../render/src/pipeline.h"
#include "groups.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    int order[ZO_MAX], err, cyc[ZO_MAX], ncyc;
    double ms;
    ZStats st;

    /* 两层：反解顺序 [1 0]，遮挡错像素为 0 */
    ZGroup g2 = zg_group_two();
    CK(zg_run(&g2, order, &err, &ms, &st, cyc, &ncyc) == 0);
    CK(err == 0);
    CK(order[0] == 1 && order[1] == 0);
    CK(st.pairs == 1 && st.zero_area == 0 && st.undecided == 0);

    /* 三层：反解顺序 [1 2 0]（真值 A<B<C） */
    ZGroup g3 = zg_group_three();
    CK(zg_run(&g3, order, &err, &ms, &st, cyc, &ncyc) == 0);
    CK(err == 0);
    CK(order[0] == 1 && order[1] == 2 && order[2] == 0);
    CK(st.pairs == 3 && st.undecided == 0);

    /* 交错：链式重叠，约束只来自相邻对 */
    ZGroup gi = zg_group_interleaved();
    CK(zg_run(&gi, order, &err, &ms, &st, cyc, &ncyc) == 0);
    CK(err == 0);
    CK(order[0] == 2 && order[1] == 3 && order[2] == 0 && order[3] == 1);
    CK(st.pairs == 3 && st.undecided == 0);

    /* 同框：四形嵌套全部两两重叠，顺序被完全约束 */
    ZGroup gf = zg_group_frame();
    CK(zg_run(&gf, order, &err, &ms, &st, cyc, &ncyc) == 0);
    CK(err == 0);
    CK(order[0] == 2 && order[1] == 3 && order[2] == 0 && order[3] == 1);
    CK(st.pairs == 6 && st.undecided == 0);

    /* 反解确实必要：按添加顺序合成会有遮挡错像素 */
    {
        ZBox win = zg_win(&g2);
        unsigned char target[900], out[900];
        int id[2] = {0, 1};
        zg_ref_render(&g2, win, 30, 30, 4, target);
        zorder_compose(g2.polys, g2.colors, g2.n, id, win, 30, 30, 4, out);
        CK(zorder_diff(out, target, 30, 30) > 0);
    }

    /* 环依赖：拒绝并指出环上图形 {0,1,2} */
    {
        ZGroup gc = zg_group_cycle();
        ZBox win = zg_win(&gc);
        int w = (int)(2.0 * (win.maxx - win.minx) + 0.5);
        int h = (int)(2.0 * (win.maxy - win.miny) + 0.5);
        static unsigned char target[2048];
        zg_cycle_target(&gc, win, w, h, 4, target);
        CK(zg_report(&gc, target, win, w, h, order, &err, &ms, &st, cyc, &ncyc) == -2);
        CK(ncyc == 3);
        CK(cyc[0] == 0 && cyc[1] == 1 && cyc[2] == 2);
        CK(st.pairs == 3);
    }

    /* 零面积接触：单独统计，不产生约束，顺序维持添加顺序 */
    {
        ZGroup gz = zg_group_zero();
        CK(zg_run(&gz, order, &err, &ms, &st, cyc, &ncyc) == 0);
        CK(st.zero_area == 2 && st.pairs == 0);
        CK(err == 0);
        CK(order[0] == 0 && order[1] == 1 && order[2] == 2 && order[3] == 3);
    }

    /* 关掉新逻辑：与旧管线逐字节一致；打开新逻辑：端到端 0 错像素 */
    {
        ZBox win = zg_win(&g3);
        RScene rs;
        scene_init(&rs);
        for (int k = 0; k < g3.n; ++k) {
            RPoint rp[64];
            for (int i = 0; i < g3.polys[k].n; ++i) {
                rp[i].x = g3.polys[k].pts[i].x;
                rp[i].y = g3.polys[k].pts[i].y;
            }
            CK(scene_add_poly(&rs, rp, g3.polys[k].n) == 0);
        }
        RBox rw = {win.minx, win.miny, win.maxx, win.maxy};
        RPlace place = {0.0, 0.0, 0.0, 1.0};
        unsigned char legacy[1600], off[1600], on[1600], target[1600];
        CK(pipeline_render(&rs, place, rw, 40, 40, legacy) == 0);
        CK(zorder_render_scene(g3.polys, 0, g3.n, 0, win, 40, 40, 4, 0, off) == 0);
        CK(memcmp(legacy, off, sizeof legacy) == 0);
        zg_ref_render(&g3, win, 40, 40, 4, target);
        CK(zorder_render_scene(g3.polys, g3.colors, g3.n, target, win, 40, 40, 4, 1, on) == 0);
        CK(zorder_diff(on, target, 40, 40) == 0);
    }

    /* 参数校验 */
    {
        ZBox win = {0, 0, 1, 1};
        unsigned char buf[4];
        CK(zorder_solve(0, 0, 0, 0, win, 1, 1, 1, order, &st, cyc, &ncyc) == -1);
        CK(zorder_render_scene(0, 0, 0, 0, win, 1, 1, 1, 0, buf) == -1);
        CK(zorder_poly_bbox(0, &win) == -1);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
