#ifndef RCULL_H
#define RCULL_H
#include "scene.h"

/* 剔除模式。
 * RCULL_OFF  ：不剔除，与改动前的渲染器逐字节一致。
 * RCULL_HAND ：手设阈值（"全凭感觉"的现方案），size < threshold 即剔。
 * RCULL_AUTO ：按期望代价定阈值，见 cull_pick_threshold。 */
enum { RCULL_OFF = 0, RCULL_HAND = 1, RCULL_AUTO = 2 };

typedef struct {
    int mode;          /* RCULL_OFF / RCULL_HAND / RCULL_AUTO */
    double threshold;  /* HAND：手设阈值，单位 px^2 */
    double lambda;     /* AUTO：漏剔一个零覆盖多边形折合的缺块像素数，必须 >= 0 */
} RCullCfg;

typedef struct {
    int npolys;        /* 输入多边形数 */
    int free_culls;    /* 与窗口无交（size==0），免费剔除，零风险 */
    int thresh_culls;  /* 被阈值剔除 */
    int kept;          /* 保留 */
    double threshold;  /* 实际使用的阈值 */
    double expected;   /* 期望缺块像素，仅 AUTO 模式填写 */
} RCullStats;

/* 多边形包围盒与窗口交集的面积，换算成 px^2。
 * 返回 0 表示不相交或零面积；此时该多边形在任何采样点都不可能被命中，
 * 覆盖必为 0，可安全免费剔除。 */
double cull_poly_size(const RPoly *p, const RBox *win, int w, int h);

/* 期望代价阈值选择。
 * sizes[i]/covs[i]：第 i 个多边形的尺寸（px^2）与实测覆盖像素数。
 * 阈值 T 的期望缺块：E(T) = sum_{size<T} cov + lambda * #{size>=T 且 cov==0}
 *   错剔：把有覆盖的多边形剔掉，损失其实际覆盖像素；
 *   漏剔：零覆盖多边形没被剔，白跑一遍光栅化，每个折 lambda 个缺块像素。
 * 在尺寸排序前缀上取使 E 最小的 T；同代价取更大阈值（剔得更多）。
 * 返回 0 并写 out_t/out_cost；
 * lambda < 0 返回 -1：负权重会把漏剔记成负代价，E 随剔除量单调下降，
 *   最优解退化为"全部剔除"，模型失去意义，拒绝；
 * n <= 0 或参数为空返回 -2（退化：无可决策对象）。 */
int cull_pick_threshold(const double *sizes, const double *covs, int n,
                        double lambda, double *out_t, double *out_cost);

#endif
