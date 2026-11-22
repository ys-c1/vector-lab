#include "src/zorder.h"
#include "groups.h"
#include <stdio.h>

int main(void) {
    int order[ZO_MAX], err, cyc[ZO_MAX], ncyc;
    double ms;
    ZStats st;
    ZGroup g;

    g = zg_group_two();
    zg_run(&g, order, &err, &ms, &st, cyc, &ncyc);
    g = zg_group_three();
    zg_run(&g, order, &err, &ms, &st, cyc, &ncyc);
    g = zg_group_interleaved();
    zg_run(&g, order, &err, &ms, &st, cyc, &ncyc);
    g = zg_group_frame();
    zg_run(&g, order, &err, &ms, &st, cyc, &ncyc);

    /* 环依赖：必须拒绝并指出环上图形 */
    {
        ZGroup gc = zg_group_cycle();
        ZBox win = zg_win(&gc);
        int w = (int)(2.0 * (win.maxx - win.minx) + 0.5);
        int h = (int)(2.0 * (win.maxy - win.miny) + 0.5);
        static unsigned char target[2048];
        zg_cycle_target(&gc, win, w, h, 4, target);
        zg_report(&gc, target, win, w, h, order, &err, &ms, &st, cyc, &ncyc);
    }

    /* 零面积接触：单独统计 */
    g = zg_group_zero();
    zg_run(&g, order, &err, &ms, &st, cyc, &ncyc);

    return 0;
}
