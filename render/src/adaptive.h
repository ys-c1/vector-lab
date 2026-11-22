#ifndef RADAPT_H
#define RADAPT_H
/*
 * 方差驱动的自适应抗锯齿采样。
 *
 * 统计模型：像素覆盖率 p 用蒙特卡洛点采样估计，单次采样服从伯努利
 * 分布（命中=1 / 未命中=0），单点方差为 sigma^2 = p(1-p)。
 *
 * 流程：
 *   1. 试点：每像素 n0 个抖动随机样本，逐像素估计单点方差（无偏 s^2）；
 *   2. 取所有像素中的最大方差作为整体估计，保证最宽像素也达标；
 *   3. 按目标置信区间全宽 W 反推所需样本数
 *          n = ceil(4 * z^2 * s2_hat / W^2)
 *      其中 z = 标准正态的 (1+conf)/2 分位数；
 *   4. n 超过 nmax 则拒绝（不产出图像），返回负状态码；
 *   5. 试点方差为 0 为退化情形：结论确定（p 为 0 或 1），无需追加样本。
 *
 * 关闭自适应逻辑时请使用 raster.c 中的 raster_fill 固定网格超采样，
 * 两条路径互不影响。
 */
#include "scene.h"

typedef struct {
    int    pilot_n;     /* 试点样本数/像素，要求 >= 2 */
    int    nmax;        /* 允许的最大样本数/像素，要求 >= pilot_n */
    double conf;        /* 置信度，0 < conf < 1，常用 0.95 */
    double width;       /* 目标置信区间全宽（覆盖率单位，0~1），要求 > 0 */
    unsigned long seed; /* 随机数种子，固定种子 => 可复现 */
} RAdaptCfg;

typedef struct {
    int    status;      /* 0 成功；负值为拒绝/错误，见下方枚举 */
    int    pilot_n;     /* 实际使用的试点样本数 */
    int    samples;     /* 实际采用的每像素样本数 n */
    int    rejected;    /* 超过 nmax 被拒时为 1 */
    int    degenerate;  /* 试点方差为 0（结论确定）时为 1 */
    double var_hat;     /* 单点方差估计（逐像素最大值，伯努利单位） */
    double width_pred;  /* 用 n 与 var_hat 预测的区间全宽 */
    double width_obs;   /* 用最终 n 个样本实测的最大区间全宽 */
    double worst_p;     /* 方差最大像素的覆盖率估计 */
    int    edge_pixels; /* 非全覆盖/非全空像素数（存在方差的像素） */
    double ms;          /* 渲染耗时（毫秒，仅采样阶段） */
} RAdaptReport;

enum {
    RADAPT_OK = 0,
    RADAPT_E_ARG = -1,
    RADAPT_E_REJECT = -2   /* 达到目标宽度所需 n 超过 nmax */
};

void radapt_cfg_default(RAdaptCfg *cfg);

/*
 * 自适应覆盖率渲染，输出 8 位灰度 PGM 缓冲区（覆盖率 * 255）。
 * 被拒（rejected）时不写入 buf。rep 可为 NULL。
 */
int raster_adaptive(const RScene *s, const RBox *box, int w, int h,
                    const RAdaptCfg *cfg, unsigned char *buf, RAdaptReport *rep);

/* 标准正态逆 CDF（Acklam 有理逼近），供测试与报告复用。 */
double norm_inv(double p);

#endif
