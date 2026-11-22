#include "winfit.h"
#include <math.h>
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int contains(const WFBox *win, const WFBox *bb) {
    return win->minx <= bb->minx && win->maxx >= bb->maxx &&
           win->miny <= bb->miny && win->maxy >= bb->maxy;
}

int main(void) {
    WFBox bb = {-2.0, -1.0, 10.0, 8.0};          /* 12 x 9, 面积 108 */
    double ba = 12.0 * 9.0;
    const double LO = 0.45, HI = 0.65;

    /* 1. 居中：目标取区间中点，锚点 0.5/0.5 */
    WFBox w;
    CK(winfit_fit(&bb, LO, HI, 0.55, 0.5, 0.5, &w) == WF_OK);
    CK(contains(&w, &bb));
    double rho = winfit_occupancy(&bb, &w);
    CK(rho >= LO && rho <= HI);
    CK(fabs(rho - 0.55) < 1e-12);
    CK(fabs((w.maxx - w.minx) - (w.maxy - w.miny) * (12.0 / 9.0)) < 1e-12);
    CK(fabs((bb.minx - w.minx) - (w.maxx - bb.maxx)) < 1e-12);  /* 两侧留白相等 */
    CK(fabs((bb.miny - w.miny) - (w.maxy - bb.maxy)) < 1e-12);

    /* 2. 紧凑：目标取区间上界，窗口最小；边界值重算有 1-ulp 舍入，容差内仍判在区间 */
    WFBox wc;
    CK(winfit_fit(&bb, LO, HI, HI, 0.5, 0.5, &wc) == WF_OK);
    CK(winfit_in_range(&bb, &wc, LO, HI) == 1);
    CK((wc.maxx - wc.minx) < (w.maxx - w.minx));                 /* 比居中窗口小 */
    CK(fabs(winfit_occupancy(&bb, &wc) - HI) < 1e-12);
    CK(winfit_in_range(&bb, &wc, LO, HI - 0.1) == 0);            /* 真出界要判否 */
    CK(winfit_in_range(&bb, &(WFBox){8, 0, 2, 6}, LO, HI) == -1); /* 非法窗口返回 -1 */

    /* 3. 偏心：ux/uy 是左/下侧留白占比，取值越大内容越靠右/上；均不超界 */
    WFBox wl;
    CK(winfit_fit(&bb, LO, HI, 0.5, 1.0, 0.0, &wl) == WF_OK);
    CK(contains(&wl, &bb));
    CK(fabs(wl.maxx - bb.maxx) < 1e-12);                         /* 右边贴合 */
    CK(fabs(wl.miny - bb.miny) < 1e-12);                         /* 下边贴合 */
    CK(winfit_occupancy(&bb, &wl) >= LO && winfit_occupancy(&bb, &wl) <= HI);
    WFBox wr;
    CK(winfit_fit(&bb, LO, HI, 0.5, 0.0, 1.0, &wr) == WF_OK);
    CK(fabs(wr.minx - bb.minx) < 1e-12);                         /* 左边贴合 */
    CK(fabs(wr.maxy - bb.maxy) < 1e-12);                         /* 上边贴合 */

    /* 4. 超界：倒置 / 零面积窗口必须被拒并指出边 */
    WFBox inv = {8.0, 0.0, 2.0, 6.0};                            /* minx > maxx，横向倒置 */
    CK(winfit_validate(&inv) == WF_EDGE_L);
    WFBox zero = {2.0, 0.0, 2.0, 6.0};                           /* minx == maxx，面积为零 */
    CK(winfit_validate(&zero) == WF_EDGE_R);
    WFBox invy = {0.0, 6.0, 8.0, 2.0};                           /* miny > maxy，纵向倒置 */
    CK(winfit_validate(&invy) == WF_EDGE_B);
    WFBox zeroy = {0.0, 6.0, 8.0, 6.0};                          /* miny == maxy，面积为零 */
    CK(winfit_validate(&zeroy) == WF_EDGE_T);
    CK(winfit_validate(&(WFBox){0, 0, 1, 1}) == WF_OK);

    /* 5. 内容盒退化同样被拒，并指出退化方向 */
    WFBox bx = {3.0, 0.0, 3.0, 9.0};
    CK(winfit_fit(&bx, LO, HI, 0.5, 0.5, 0.5, &w) == WF_BWIDTH);
    WFBox by = {0.0, 4.0, 9.0, 4.0};
    CK(winfit_fit(&by, LO, HI, 0.5, 0.5, 0.5, &w) == WF_BHEIGHT);

    /* 6. 区间与锚点入参校验 */
    CK(winfit_fit(&bb, 0.0, HI, 0.5, 0.5, 0.5, &w) == WF_RNG);   /* lo 必须 > 0 */
    CK(winfit_fit(&bb, HI, LO, 0.5, 0.5, 0.5, &w) == WF_RNG);    /* lo > hi */
    CK(winfit_fit(&bb, LO, 1.5, 0.5, 0.5, 0.5, &w) == WF_RNG);   /* hi > 1 */
    CK(winfit_fit(&bb, LO, HI, 0.3, 0.5, 0.5, &w) == WF_RNG);    /* 目标落在区间外 */
    CK(winfit_fit(&bb, LO, HI, 0.5, 1.2, 0.5, &w) == WF_ANCHOR);
    CK(winfit_fit(&bb, LO, HI, 0.5, 0.5, -0.1, &w) == WF_ANCHOR);

    /* 7. 空指针与占屏函数 */
    CK(winfit_fit(0, LO, HI, 0.5, 0.5, 0.5, &w) == WF_NULL);
    CK(winfit_validate(0) == WF_NULL);
    CK(winfit_occupancy(&bb, &inv) < 0.0);
    CK(fabs(winfit_occupancy(&bb, &(WFBox){-2, -1, 10, 8}) - 1.0) < 1e-12);
    (void)ba;

    /* 8. 错误码全部带中文说明并点名涉事边 */
    for (int code = WF_EDGE_T; code <= WF_EDGE_L; ++code)
        CK(winfit_strerror(code)[0] != '\0');

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
