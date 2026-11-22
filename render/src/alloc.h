#ifndef RALLOC_H
#define RALLOC_H
#include "scene.h"

/* 预算分配：在总采样预算内同时挑出图尺寸 (w,h) 与超采样 ss。
 *
 * 代价模型  cost = w * h * ss * ss
 *   w*h      出图像素数（尺寸）
 *   ss*ss    每像素子采样数（超采样）
 *   两者吃同一份预算，相乘就是实际采样次数。
 *
 * 锯齿指标 J（越小越好）：
 *   把候选渲染出的覆盖率图按面积加权缩放到一块固定"显示网格"
 *   （最长边 ref_side 像素），计算
 *       J = mean |放大图 - 精确覆盖率|  +  w_stair * mean |二阶差分|
 *   精确覆盖率 = 场景（并集）与每个显示格子的精确相交面积 / 格子面积。
 *   第一项是几何误差，第二项是边缘阶梯（锯齿）能量。
 *
 *   为什么必须固定显示网格：不固定的话 J 会退化成"像素越多越好"，
 *   超采样永远分不到预算；固定之后，
 *     - 尺寸不足显示网格时，图被放大，出现块状阶梯，J 变差；
 *     - 尺寸达到显示网格后，再加大尺寸只是给下采样多送样本，收益递减；
 *     - 超采样直接提高每格灰阶精度，与尺寸在同等样本成本上竞争。
 *
 * 单调性靠结构保证：候选集（尺寸阶梯 × ss=1..8）与预算无关，
 *   预算只筛掉 cost 超支的候选；预算变大时可行集只增不减，
 *   上一档的赢家始终仍在可行集里且 J 已缓存，所以最优 J 单调不升。
 */

#define RALLOC_REF_SIDE_DEFAULT 256
/* 锯齿指标的两项权重：与精确覆盖率的 L1 误差 + 该项 × 显示网格二阶差分。 */
#define RALLOC_STAIR_WEIGHT 1.0
#define RALLOC_MAX_SS 8
#define RALLOC_MIN_SIDE 2
#define RALLOC_MAX_CAND 2048
#define RALLOC_PHASES 2

typedef struct {
    int w, h;          /* 出图尺寸 */
    int ss;            /* 超采样倍数 */
    long cost;         /* w*h*ss*ss */
    double jag;        /* 锯齿指标；<0 表示尚未评测 */
} RAlloc;

enum {
    RALLOC_OK          = 0,
    RALLOC_BAD_INPUT   = -1,
    RALLOC_BUDGET_ZERO = -2,   /* 预算为 0（或负）：明确报出，不静默 */
    RALLOC_NO_FIT      = -3,   /* 预算非零，但连最小候选都放不下 */
    RALLOC_OVER_SIZE   = -10,  /* 尺寸本身超预算 */
    RALLOC_OVER_SS     = -11,  /* 尺寸可行，但加上超采样后超预算 */
    RALLOC_OVER_BOTH   = -12,  /* 尺寸超支，且 ss 也超出 1..8 */
    RALLOC_OVER_RANGE  = -13   /* ss 超出 1..8（尺寸未超支） */
};

typedef struct {
    long budget;
    int sizes;          /* 尺寸档数 */
    int candidates;     /* 候选 (w,h,ss) 总数 */
    int evaluated;      /* 已评测 J 的候选数 */
    int ref_w, ref_h;   /* 显示网格 */
    double plan_ms;     /* 建参照/候选耗时 */
    double eval_ms;     /* 评测耗时 */
} RAllocReport;

typedef struct {
    RScene scene;          /* 场景副本（调用方先做好变换） */
    RBox win;
    int ref_w, ref_h;
    double *ref;           /* 精确覆盖率，RALLOC_PHASES 个平面 */
    double phase_ox[RALLOC_PHASES], phase_oy[RALLOC_PHASES];
    RAlloc cand[RALLOC_MAX_CAND];
    int n;
    int evaluated;
    double eval_ms;
} RAllocPlan;

/* 建候选表与精确参照。win 为栅格化窗口（可为 NULL，默认场景 bbox）。
 * ref_side<=0 用 RALLOC_REF_SIDE_DEFAULT。max_budget 决定候选表上界。 */
int ralloc_plan(const RScene *s, const RBox *win, int ref_side, long max_budget,
                RAllocPlan *p, RAllocReport *rep);
void ralloc_plan_free(RAllocPlan *p);

/* 在预算内挑最优候选。惰性评测并缓存 J。
 * 返回 RALLOC_OK / RALLOC_BUDGET_ZERO / RALLOC_NO_FIT。 */
int ralloc_pick(RAllocPlan *p, long budget, RAlloc *out);

/* 评测单个 (w,h,ss) 的 J（不缓存）。 */
double ralloc_eval(RAllocPlan *p, int w, int h, int ss);

/* plan + pick 一次完成（内部会释放）。 */
int ralloc_solve(const RScene *s, const RBox *win, long budget, int ref_side,
                 RAlloc *out, RAllocReport *rep);

/* 越界检查：在预算内返回 0；越界返回负值，并在 msg 里写明超了哪一项。 */
int ralloc_check(int w, int h, int ss, long budget, char *msg, int msglen);

#endif
