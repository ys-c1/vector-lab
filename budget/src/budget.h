#ifndef BUDGET_H
#define BUDGET_H

#include <stdio.h>

#define BUDGET_MAX_SEG 8
#define BUDGET_MAX_MOD 16

/* 单模块收益曲线：分段线性，第 k 段宽 width[k]、边际收益 slope[k]。
 * 合法曲线：段数在 [1, BUDGET_MAX_SEG]，width>0 且有限，
 * slope>=0 且有限，且 slope 非增（曲线为凹，边际收益递减）。 */
typedef struct {
    const char *name;
    int nseg;
    double width[BUDGET_MAX_SEG];
    double slope[BUDGET_MAX_SEG];
} BCurve;

typedef struct {
    double x;    /* 分到的预算 */
    double gain; /* 该预算下的累计收益 */
} BAlloc;

enum {
    BUDGET_OK = 0,
    BUDGET_ERR_ARG = -1,    /* 参数为空或模块数越界 */
    BUDGET_ERR_BUDGET = -2, /* 预算为零或为负，拒绝分配 */
    BUDGET_ERR_NSEG = -3,   /* 收益曲线非法：段数越界 */
    BUDGET_ERR_WIDTH = -4,  /* 收益曲线非法：段宽非正或非有限 */
    BUDGET_ERR_SLOPE = -5,  /* 收益曲线非法：边际收益为负或非有限 */
    BUDGET_ERR_CONVEX = -6  /* 收益曲线非法：边际收益递增，曲线非凹 */
};

const char *budget_strerror(int code);

int budget_curve_check(const BCurve *c);
int budget_curve_is_zero(const BCurve *c); /* 收益是否恒为零 */
double budget_curve_cap(const BCurve *c);  /* 曲线总容量 */
double budget_curve_gain(const BCurve *c, double x);

/* 拉格朗日松弛分配：按边际收益相等（注水法）求解，
 * 使 sum gain 最大、sum x = min(budget, 总容量)，lambda 为边际收益水平。
 * 收益恒为零的模块不参与分配，下标写入 zero_idx（可为 NULL）单独列出。
 * 曲线非法或预算为零时拒绝，返回负错误码。 */
int budget_lagrange(const BCurve *cs, int n, double budget,
                    BAlloc *out, int *zero_idx, int *nzero, double *lambda);

/* 旧版均分：所有模块（含零收益模块）平分预算，超出容量部分浪费。 */
int budget_equal(const BCurve *cs, int n, double budget, BAlloc *out);

/* 标准演示模块集与四档预算 */
int budget_std_curves(BCurve *out, int max);
int budget_std_tiers(double *out, int max);

/* 分配报告。use_lagrange=0 时为旧版均分报告，输出与历史版本逐字节相同。 */
int budget_report(FILE *fp, const BCurve *cs, int n,
                  const double *tiers, int ntier, int use_lagrange);

#endif
