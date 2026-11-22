#ifndef SIMPLIFY_H
#define SIMPLIFY_H

/* 路径抽稀：全局 RDP（旧逻辑）+ 动态规划分段抽稀（新逻辑）。
 * 新逻辑：按弧长把路径分成若干段，每段用 DP 决定保留点数，
 * 在满足总偏差上界的前提下保留点数最少；分段边界确定可复现。
 * use_dp=0 时走旧的全局抽稀，输出与其逐字节一致。 */

typedef struct { double x, y; } SPoint;

#define SP_MAX_PTS 512
#define SP_MAX_SEG 32

typedef struct {
    int count;                  /* 保留点数 */
    SPoint pts[SP_MAX_PTS];     /* 保留点，按原顺序 */
    int idx[SP_MAX_PTS];        /* 保留点在原路径中的下标 */
} SPoly;

typedef struct {
    int use_dp;      /* 0 = 旧全局抽稀；1 = DP 分段抽稀 */
    double eps;      /* 旧逻辑容差（use_dp=0 时生效） */
    int segments;    /* 分段数（use_dp=1 时生效），0 拒绝 */
    double max_dev;  /* 总偏差上界（use_dp=1 时生效），负数拒绝 */
} SOpts;

typedef struct {
    int segments;                   /* 实际分段数（旧逻辑为 0） */
    int kept;                       /* 保留点数 */
    double total_dev;               /* 总偏差（各段偏差之和） */
    double elapsed_ms;              /* 耗时，毫秒 */
    int boundary[SP_MAX_SEG + 1];   /* 分段边界下标，确定可复现 */
} SStats;

/* 错误码 */
#define SP_OK        0
#define SP_ERR_ARG  -1   /* 空指针等参数错误 */
#define SP_ERR_NPTS -2   /* 点不足二 */
#define SP_ERR_SEG0 -3   /* 段数为零 */
#define SP_ERR_SEGN -4   /* 段数超过 n-1，无法每段至少两点 */
#define SP_ERR_DEV  -5   /* 偏差上界为负 */
#define SP_ERR_BIG  -6   /* 点数超过 SP_MAX_PTS */
#define SP_ERR_MEM  -7   /* 内存分配失败 */

const char *sp_strerror(int rc);

/* 旧逻辑：全局 Douglas-Peucker 抽稀。 */
int sp_simplify_global(const SPoint *pts, int n, double eps, SPoly *out);

/* 统一入口：use_dp=0 时与 sp_simplify_global 输出逐字节一致。 */
int sp_simplify(const SPoint *pts, int n, const SOpts *opt, SPoly *out, SStats *st);

#endif
