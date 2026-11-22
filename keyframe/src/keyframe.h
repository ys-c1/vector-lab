#ifndef KEYFRAME_H
#define KEYFRAME_H

#include <stdio.h>

/*
 * 关键帧之间按最小作用量补中间点。
 *
 * 目标：把加速度平方沿时间积分 S = ∫ |q''(t)|^2 dt 当作作用量，
 * 固定各关键帧的时刻与位置，解出中间采样点让 S 最小。离散形式
 * 给每个节点 k 的速度用相邻两点差分，加速度用中心差分：
 *
 *     v_k^- = (q_k - q_{k-1}) / h_{k-1}
 *     v_k^+ = (q_{k+1} - q_k) / h_k
 *     a_k   = 2 * (v_k^+ - v_k^-) / (h_{k-1} + h_k)
 *
 * 每段长度上对 a^2 的梯形积分即 w_k * |a_k|^2，w_k = (h_{k-1}+h_k)/2。
 * 首末节点没有加速度项（端点处取零权），整段按 S = Σ w_k |a_k|^2
 * 最小化——这是一个对称正定的稀疏二次型，逐轴各解一次线性方程组
 * （三对角，Cholesky 分解）。
 *
 * 节点布局：每两个关键帧之间均匀插入 levels 个间隔（levels 是
 * 「每段间隔数」，KEY_MIN_LEVELS..KEY_MAX_LEVELS）。关键帧落在段首
 * 节点上，段内其余节点自由。每段中点取时间中点上的位置：levels 为
 * 偶数时它正好是节点，为奇数时取中点两侧节点的线性插值（不引入
 * 新的自由度）。
 *
 * 线性插值（兼容路径）：flags 不含 KEY_F_MIN_ACTION 时不做任何求解，
 * 全部节点直接取相邻关键帧的线性插值。编译期把 KEYFRAME_MIN_ACTION
 * 置 0（走 key_solve_default）即整模块退回线性插值；该路径的节点表
 * 与中点与 baseline/linear_ref.c 的独立参照逐字节一致（make compat
 * 比对），保证关闭新逻辑时输出不变。
 *
 * 每段作用量：把每个节点的加速度项归到它所在的段；关键帧节点被
 * 前后两段共用，该项对半计入两段（段作用量之和等于整段 S）。
 * 每段比较「平滑中间点」与「线性中间点」两条轨道各自的作用量，
 * 报出降幅。只有两帧时没有内部节点：两头钳死的离散二阶差分能量
 * 最小值就是直线本身，平滑解与线性解逐位相同（即线性中点），两轨
 * 作用量都是零，降幅按 0 报告并标记 degenerate。
 */

#define KEY_MAX_KEYS    16   /* 关键帧上限                       */
#define KEY_MAX_LEVELS  16   /* 每段间隔数上限                   */
#define KEY_MIN_LEVELS  2    /* 每段至少一个中间节点             */
#define KEY_MAX_NODES   ((KEY_MAX_KEYS - 1) * KEY_MAX_LEVELS + 1)

/* 默认编译开关：1 = 走最小作用量，0 = 退回线性插值。
 * compat 目标用 -DKEYFRAME_MIN_ACTION=0 编译，验证线性路径逐字节不变。 */
#ifndef KEYFRAME_MIN_ACTION
#define KEYFRAME_MIN_ACTION 1
#endif

/* 返回码；失败时 err_at 给关键帧序号（或 -1 表示整体参数）。 */
enum {
    KEY_OK = 0,
    KEY_E_ARG = -1,      /* 空指针 / 参数错误，err_at = -1             */
    KEY_E_FEW = -2,      /* 帧数不足（n < 2 或 n > KEY_MAX_KEYS），
                          * err_at = 传入的帧数                       */
    KEY_E_TIME = -3,     /* 时间倒置或相等，err_at = 第一个出错帧号    */
    KEY_E_NONFINITE = -4,/* 中间帧坐标或时间非有限，err_at = 出错帧号  */
    KEY_E_LEVELS = -5,   /* levels 越界，err_at = -1                   */
    KEY_E_FLAGS = -6,    /* 未知 flags 位，err_at = -1                 */
    KEY_E_ENDPOINT = -7, /* 首帧或末帧端点缺失（坐标非有限），err_at 为
                          * 0 或 n-1；端点优先于中间帧报告             */
    KEY_E_SOLVE = -8     /* 线性方程组非正定（数值退化），err_at = -1  */
};

#define KEY_F_MIN_ACTION 1u  /* 打开最小作用量求解；关闭时线性插值 */

typedef struct { double t, x, y; } KeyFrame;

/* 一帧的输出：时刻与位置。 */
typedef struct { double t, x, y; } KeySample;

/* 一段（相邻两关键帧之间）的结果。 */
typedef struct {
    int from, to;        /* 两端关键帧序号                          */
    double action;       /* 平滑轨在该段的作用量份额                */
    double linear_action;/* 线性轨在同一段的作用量份额              */
    double reduction;    /* 1 - action/linear_action，线性作用量为
                          * 零或退化时记 0                         */
    KeySample mid;       /* 该段时间中点的位置                      */
    int degenerate;      /* 1 = 只有两帧，平滑解退化为线性          */
} KeySpan;

/* 求解结果。 */
typedef struct {
    int nkeys;
    int levels;
    int nnodes;                  /* 节点总数 (nkeys-1)*levels+1   */
    double total_action;         /* 平滑轨整段作用量              */
    double total_linear_action;  /* 线性轨整段作用量              */
    double total_reduction;      /* 1 - total_action/total; 退化记 0 */
    int degenerate;              /* 1 = 只有两帧                   */
    int nspans;
    KeySpan spans[KEY_MAX_KEYS - 1];
    KeySample nodes[KEY_MAX_NODES]; /* 全部节点，含两端关键帧     */
} KeyPlan;

const char *key_strerror(int code);

/*
 * 排中间点。
 *   keys/n   关键帧，t 必须严格递增（相等或倒置拒绝）
 *   levels   每段间隔数，KEY_MIN_LEVELS..KEY_MAX_LEVELS
 *   flags    KEY_F_MIN_ACTION 打开最小作用量；否则线性插值
 *   out      结果
 *   err_at   出错时的帧号（可为 NULL）
 * 返回 KEY_OK 或上方错误码。
 */
int key_solve(const KeyFrame *keys, int n, int levels, unsigned flags,
              KeyPlan *out, int *err_at);

/* 按编译期默认开关求解：KEYFRAME_MIN_ACTION 为 1 时等价于
 * key_solve(..., KEY_F_MIN_ACTION, ...)，为 0 时等价于线性插值。 */
int key_solve_default(const KeyFrame *keys, int n, int levels,
                      KeyPlan *out, int *err_at);

/* 在节点表上按时间取位置（节点之间线性插值），越界钳到端点。
 * 返回 0 成功，-1 参数非法。 */
int key_sample_at(const KeyPlan *plan, double t, KeySample *out);

/* 逐段表导出 CSV：from,to,mid_t,mid_x,mid_y,action,linear_action,
 * reduction,degenerate。path 为 NULL 或 "-" 时写 stdout。 */
int key_export_csv(const KeyPlan *plan, const char *path);

#endif
