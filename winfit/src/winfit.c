#include "winfit.h"
#include <math.h>

int winfit_fit(const WFBox *bb, double lo, double hi,
               double rho_t, double ux, double uy, WFBox *out) {
    if (!bb || !out) return WF_NULL;
    double bw = bb->maxx - bb->minx;
    double bh = bb->maxy - bb->miny;
    /* 内容盒自身退化：先拒，并指出退化发生在哪一对边 */
    if (!(bw > 0.0)) return WF_BWIDTH;
    if (!(bh > 0.0)) return WF_BHEIGHT;
    if (!(lo > 0.0 && lo <= hi && hi <= 1.0)) return WF_RNG;
    if (!(rho_t >= lo && rho_t <= hi)) return WF_RNG;
    if (!(ux >= 0.0 && ux <= 1.0)) return WF_ANCHOR;
    if (!(uy >= 0.0 && uy <= 1.0)) return WF_ANCHOR;

    /* 统一相对扩张：k = 1/sqrt(rho_t)，则 rho = ba / (W H) = rho_t */
    double k = 1.0 / sqrt(rho_t);
    double W = k * bw, H = k * bh;
    double padx = W - bw, pady = H - bh;

    out->minx = bb->minx - ux * padx;
    out->maxx = bb->maxx + (1.0 - ux) * padx;
    out->miny = bb->miny - uy * pady;
    out->maxy = bb->maxy + (1.0 - uy) * pady;

    /* 反解结果再走一遍校验：倒置或零面积即拒，并指出是哪条边 */
    return winfit_validate(out);
}

int winfit_validate(const WFBox *win) {
    if (!win) return WF_NULL;
    if (!(win->maxx > win->minx)) {
        /* 左、右两条边共同决定横向合法性，分别报告便于定位 */
        if (win->minx > win->maxx) return WF_EDGE_L;
        return WF_EDGE_R;
    }
    if (!(win->maxy > win->miny)) {
        if (win->miny > win->maxy) return WF_EDGE_B;
        return WF_EDGE_T;
    }
    return WF_OK;
}

double winfit_occupancy(const WFBox *bb, const WFBox *win) {
    if (!bb || !win) return -1.0;
    double bw = bb->maxx - bb->minx;
    double bh = bb->maxy - bb->miny;
    double W = win->maxx - win->minx;
    double H = win->maxy - win->miny;
    if (!(bw > 0.0) || !(bh > 0.0) || !(W > 0.0) || !(H > 0.0)) return -1.0;
    return (bw * bh) / (W * H);
}

int winfit_in_range(const WFBox *bb, const WFBox *win, double lo, double hi) {
    double rho = winfit_occupancy(bb, win);
    if (rho < 0.0 || !(lo > 0.0 && lo <= hi && hi <= 1.0)) return -1;
    double tol = WF_RHO_EPS * (hi > 0.0 ? hi : 1.0);
    return rho >= lo - tol && rho <= hi + tol;
}

const char *winfit_strerror(int code) {
    switch (code) {
    case WF_OK:       return "ok";
    case WF_NULL:     return "拒绝：入参为空指针";
    case WF_BWIDTH:   return "拒绝：内容盒横向退化（minx >= maxx，左/右边倒置或宽度为零）";
    case WF_BHEIGHT:  return "拒绝：内容盒纵向退化（miny >= maxy，下/上边倒置或高度为零）";
    case WF_RNG:      return "拒绝：占屏比例区间非法（要求 0 < lo <= hi <= 1 且 rho_t 落在区间内）";
    case WF_ANCHOR:  return "拒绝：锚点越界（ux/uy 必须在 [0,1]）";
    case WF_EDGE_L:   return "拒绝：窗口左边非法（minx > maxx，窗口横向倒置，涉事边：左 minx）";
    case WF_EDGE_R:   return "拒绝：窗口右边非法（minx == maxx，窗口横向面积为零，涉事边：右 maxx）";
    case WF_EDGE_B:   return "拒绝：窗口下边非法（miny > maxy，窗口纵向倒置，涉事边：下 miny）";
    case WF_EDGE_T:   return "拒绝：窗口上边非法（miny == maxy，窗口纵向面积为零，涉事边：上 maxy）";
    default:          return "拒绝：未知错误";
    }
}
