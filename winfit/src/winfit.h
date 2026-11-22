#ifndef WINFIT_H
#define WINFIT_H

/* 窗口框反解模块：给定内容包围盒与目标占屏比例区间，反推窗口四边。
 *
 * 占屏比例 rho 定义为内容包围盒面积与窗口面积之比：
 *     rho = (bw * bh) / (W * H)
 * 窗口四边必须完整包围内容盒（minx <= 盒左、maxx >= 盒右，纵向同理）。
 *
 * 反解方法：取统一相对扩张系数 k = 1/sqrt(rho_t)，令
 *     W = k*bw, H = k*bh，即 rho = 1/k^2 = rho_t
 * 再由锚点 (ux, uy) 分配两侧留白：ux/uy 为内容盒左侧/下侧留白占总留白的
 * 比例，0.5 居中，趋 0 贴右/上，趋 1 贴左/下，取值限定 [0,1] 保证不超界。
 */

typedef struct { double minx, miny, maxx, maxy; } WFBox;

/* 返回码 */
enum {
    WF_OK = 0,
    WF_NULL = -1,      /* 空指针 */
    WF_BWIDTH = -2,    /* 内容盒宽为零或倒置：minx >= maxx */
    WF_BHEIGHT = -3,   /* 内容盒高为零或倒置：miny >= maxy */
    WF_RNG = -4,       /* 占屏区间非法：要求 0 < lo <= hi <= 1 */
    WF_ANCHOR = -5,    /* 锚点越界：ux/uy 不在 [0,1] */
    WF_EDGE_L = -6,    /* 窗口左边非法：minx >= maxx */
    WF_EDGE_R = -7,    /* 窗口右边非法：minx >= maxx */
    WF_EDGE_B = -8,    /* 窗口下边非法：miny >= maxy */
    WF_EDGE_T = -9     /* 窗口上边非法：miny >= maxy */
};

/* 反解窗口框。
 * bb     内容包围盒
 * lo,hi  目标占屏比例区间（闭区间），rho_t 为本次瞄准的目标值，需落在区间内
 * ux,uy  锚点，各维度两侧留白的分配比例，0.5 = 居中
 * out    反解出的窗口四边
 */
int winfit_fit(const WFBox *bb, double lo, double hi,
               double rho_t, double ux, double uy, WFBox *out);

/* 校验已有的窗口框：倒置或面积为零时拒，并指出是哪条边。 */
int winfit_validate(const WFBox *win);

/* 计算实际占屏比例；非法入参返回负数。 */
double winfit_occupancy(const WFBox *bb, const WFBox *win);

/* 占屏区间判定容差：反解四边经浮点四则运算后重算 rho，与目标值存在
 * 1-ulp 级偏差，区间判定统一放宽 WF_RHO_EPS（相对量）。 */
#define WF_RHO_EPS 1e-12

/* 占屏比例是否落在 [lo, hi] 内（含边界，带 WF_RHO_EPS 容差）。
 * 合法且在区间内返回 1，不在区间返回 0，入参非法返回 -1。 */
int winfit_in_range(const WFBox *bb, const WFBox *win, double lo, double hi);

/* 错误码 -> 中文说明（含涉事边）。 */
const char *winfit_strerror(int code);

#endif
