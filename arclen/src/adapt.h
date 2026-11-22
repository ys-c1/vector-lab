#ifndef ADAPT_H
#define ADAPT_H
#include "arclen.h"

/* 自适应弧长表：由允许插值误差上界反解最大允许采样间隔，再按该间隔建表。
 * 与 arclen_build 的等间隔表完全独立；enable=0 时调用方应走 arclen_build。
 */
typedef struct {
    int    count;        /* 表长（节点数，>=2） */
    double eps;          /* 允许的插值误差上界 */
    double max_interval; /* 反解出的最大允许间隔 */
    double total;        /* 曲线总弧长 */
    double kmax;         /* 估计的最大曲率 */
    double *s;           /* 累计弧长，count 个 */
    double *x, *y;       /* 节点坐标，各 count 个 */
} AdaptiveTable;

typedef struct {
    double eps; /* 允许的插值误差上界，须 > 0 */
} AdaptConfig;

/* 返回码：
 *   0 成功
 *  -1 非法参数（空指针 / 曲线点少于二 / eps<=0 / 总长为零等）
 *  -2 反解间隔大于曲线总长，拒绝建表
 *  -3 内存不足
 */
int adapt_build(const APoint *pts, int n, const AdaptConfig *cfg, AdaptiveTable *out);

/* 按弧长 s 线性插值取点；s 越界返回 -2 */
int adapt_sample(const AdaptiveTable *t, double s, APoint *out);

/* 在密集弧长网格上把表的线性插值结果与原始曲线逐点比较，
 * 返回最大实际插值误差（>0），失败返回负数。 */
double adapt_verify(const APoint *pts, int n, const AdaptiveTable *t, int probes);

void adapt_free(AdaptiveTable *t);
#endif
