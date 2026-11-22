#ifndef RADAPT_H
#define RADAPT_H
#include "scene.h"

/*
 * 自适应超采样：在边缘锯齿指标达标的前提下，用二分法反解最小 ss。
 *
 * 指标定义：以 SS_REF x SS_REF 细网格、与光栅器相同的 any-hit 规则
 * （任一子样本命中即 on）作为参照，只在边缘带（0 < 覆盖率 < 1 的
 * 像素）上统计候选渲染结果与参照的不一致比例，范围 [0, 1]。
 *
 * 合法 ss 域为 [1, 8]。探针回调只接收 ss 一个变量，场景/窗口/尺寸
 * 全部封在调用方上下文里，保证“每次只改 ss，其它不动”。
 */

#define ADAPT_SS_MIN 1
#define ADAPT_SS_MAX 8
#define ADAPT_SS_REF 32
#define ADAPT_EPS 1e-12

enum {
    ADAPT_OK = 0,
    ADAPT_ERR_ARG = -1,       /* ss<=0、ss>8、空指针、空场景、阈值非法等 */
    ADAPT_ERR_NOPASS = -2,    /* ss=8 指标仍不达标 */
    ADAPT_ERR_NONMONO = -3    /* 指标随 ss 上升，拒绝并给出双方数值 */
};

/* 32x32 覆盖率参照：cov 为每个像素命中子样本数（0..SS_REF^2）。 */
typedef struct {
    const RScene *scene;
    const RBox *box;
    int w, h;
    int *cov;                 /* 长度 w*h，调用方分配 */
} AdaptRef;

/* 探针：只改 ss，其它参数由 ctx 固定。返回 0 成功，<0 为渲染失败。 */
typedef int (*AdaptProbeFn)(int ss, void *ctx, unsigned char *buf);

typedef struct {
    AdaptProbeFn probe;
    void *ctx;
    const AdaptRef *ref;
    double tol;               /* 达标阈值：边缘带不一致比例 <= tol */
    int ss_lo, ss_hi;         /* 二分区间，必须落在 [1, 8] */
    unsigned char *buf;       /* 渲染缓冲，长度 ref->w * ref->h */
} AdaptTarget;

typedef struct {
    int ss;                   /* 达标最小 ss；NOPASS/NONMONO 时为 8/探针值 */
    double metric;            /* 最终 ss 对应的锯齿指标 */
    int edge_pixels;          /* 参照边缘带像素数 */
    int bad_pixels;           /* 边缘带内与参照不一致的像素数 */
    int nprobes;              /* 二分探针次数（不含参照构建） */
    double probe_ms[ADAPT_SS_MAX + 1]; /* 各 ss 探针耗时，未探测为 -1 */
    double metric_at[ADAPT_SS_MAX + 1];
    int mono_a, mono_b;       /* NONMONO 时：mono_b 的指标高于 mono_a */
    double metric_a, metric_b;
} AdaptResult;

/* 构建高采样覆盖率参照。 */
int adapt_ref_build(AdaptRef *ref);

/* 用给定 ss 渲染 buf 并计算边缘锯齿指标；band/bad 可为 NULL。 */
int adapt_edge_metric(const AdaptRef *ref, int ss,
                      AdaptProbeFn probe, void *ctx, unsigned char *buf,
                      double *metric, int *band, int *bad);

/* 二分反解达标最小 ss。 */
int adapt_solve(const AdaptTarget *t, AdaptResult *r);

const char *adapt_strerror(int code);

#endif
