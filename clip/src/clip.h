#ifndef CLIP_H
#define CLIP_H
#include <stddef.h>

typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[64]; } CPoly;

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);
double clip_area(const CPoly *p);

/* ---- 多窗口顺序裁决（新逻辑） ----
 * 枚举窗口的全部排列，逐排列连续裁剪，按累积误差最小者定顺序。
 * 误差累积方式：
 *   对排列 pi 的第 k 步，P_k 为按该顺序连续裁剪得到的实际多边形；
 *   Q_k 为前缀窗口的凸交集多边形（依次 clip 窗口本身得到）；
 *   S_k 为源多边形对 Q_k 的一次性裁剪（理想参照，顺序无关）；
 *   e_k = |area(P_k) - area(S_k)|，累积误差 E(pi) = Σ_{k=1..m} e_k。
 * 非法排列（不参与裁决，reject 记录原因）：
 *   1) 某窗口为空（count<3 或退化面积 <= CLIP_EPS_ABS）；
 *   2) 某两窗口完全重合（重复窗口，交集面积与两者各自面积相对差均 < 1e-9）。
 * 返回 0 成功；-1 参数错误；-2 窗口数超 CLIP_MAX_WINS；
 * -3 全部排列非法；-4 内存不足。
 */
#define CLIP_MAX_WINS 8
#define CLIP_EPS_ABS 1e-12

#define CLIP_REASON_EMPTY   1   /* 窗口为空/退化 */
#define CLIP_REASON_DUPWIN  2   /* 重复窗口（完全重合） */

typedef struct {
    int order[CLIP_MAX_WINS];   /* 排列内容：窗口下标，0 基 */
    int legal;                  /* 1 可裁决；0 非法 */
    int reason;                 /* 非法原因，CLIP_REASON_* */
    int bad_a, bad_b;           /* 非法涉及的两个窗口下标（DUPWIN）；
                                   EMPTY 时 bad_a 为空窗口下标，bad_b=-1 */
    double cum_err;             /* 合法排列的累积误差 */
    double step_err[CLIP_MAX_WINS]; /* 各步误差 e_k */
    double time_ns;             /* 该排列评估耗时（纳秒） */
} CPermResult;

typedef struct {
    int nperm;                  /* m! 个排列 */
    int best;                   /* 最优排列在 res 中的下标；-1 表示全部非法 */
    int best_order[CLIP_MAX_WINS];
    double best_err;
    double total_ns;            /* 全量枚举耗时（纳秒） */
    CPermResult res[40320];     /* 8! 上界 */
} CVerdict;

int clip_verdict(const CPoly *poly, const CPoly *wins, int m, CVerdict *v);
size_t clip_factorial(int m);

/* 判定窗口是否为空/退化：count<3 或面积绝对值 <= CLIP_EPS_ABS */
int clip_window_empty(const CPoly *w);
/* 判定两窗口是否完全重合（重复窗口） */
int clip_window_duplicate(const CPoly *a, const CPoly *b);
/* 前缀窗口交集多边形 Q：调用方保证各窗口非空 */
void clip_prefix_intersection(const CPoly *wins, const int *order, int k, CPoly *q);
/* 高稳定面积（Kahan 求和），用于误差裁决 */
double clip_area_kahan(const CPoly *p);

#endif
