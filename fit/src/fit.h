#ifndef FIT_H
#define FIT_H

/*
 * 角点落点拟合：刚性相似变换（单位缩放），三个未知量。
 *
 *   X = cos(rot) * x - sin(rot) * y + tx
 *   Y = sin(rot) * x + cos(rot) * y + ty
 *
 * 未知量为 (rot, tx, ty) 三个。令 a = cos(rot), b = sin(rot) 线性化为
 * (a, b, tx, ty)，每个角点给出两条线性方程，全部角点拼成同一张 4 列
 * 设计矩阵、一次性最小二乘解出；再单位化 (a,b) 施加刚性约束 a^2+b^2=1
 * 反算转角，最后回代计算每个角点的残差。坐标与残差均在同一像素坐标系
 * 下，因此半像素容差即为 0.5（像素）。
 *
 * 秩亏来源：所有源角点重合（转角不可观测，设计矩阵秩为 2）。
 * 源角点共线在该模型下不秩亏；数据不相容时体现为残差超差。
 */

typedef struct { double x, y; } FPoint;

typedef struct {
    double rot;              /* 弧度，已归一化到 (-pi, pi] */
    double tx, ty;           /* 像素坐标系下的平移 */
    double scale_unused;     /* 预留：本模型恒为 1.0 */
} FTransform;

typedef struct {
    double min_scale;        /* 本模型 scale 恒为 1.0，用于越界校验，<=0 表示不校验 */
    double max_scale;
    double max_rot_abs;      /* |rot| 上限（弧度），<=0 表示不校验 */
    double max_t_abs;        /* |tx|、|ty| 上限（像素），<=0 表示不校验 */
    double pixel_tol;        /* 残差容差（像素），典型 0.5 */
} FBounds;

/* 返回码 */
#define FIT_OK            0   /* 解出且残差、越界检查全部通过 */
#define FIT_E_INPUT      (-1) /* 入参非法 */
#define FIT_E_DEGENERATE (-2) /* 方程退化（秩不足），conflict 字段给出冲突的两条 */
#define FIT_E_RESIDUAL   (-3) /* 解出但最大残差超过 pixel_tol */
#define FIT_E_BOUNDS     (-4) /* 解出但未知量越界 */

typedef struct {
    int status;
    FTransform tr;
    double max_residual;          /* 所有角点中的最大平面残差（像素） */
    double rms_residual;
    double per_corner[64];        /* 每个角点的平面残差（像素） */
    int    conflict_a, conflict_b;/* 退化时互相冲突的两条方程对应的角点下标，-1 表示无 */
    char   conflict_axis[2][2];   /* "x" 或 "y"：冲突方程各自是哪一条 */
    char   detail[256];           /* 人类可读说明 */
    double solve_ns;              /* 求解耗时（纳秒） */
} FFitResult;

/*
 * 线性方程组求解（新逻辑）。n 组角点对应关系，src[i] 映射到 dst[i]。
 * n >= 2 即可解；角点重合/共线导致秩不足时返回 FIT_E_DEGENERATE。
 */
FFitResult fit_solve_linear(const FPoint *src, const FPoint *dst, int n,
                            const FBounds *bounds);

/*
 * 旧的逐个试（粗到细网格搜索）逻辑，仅用于回归比对。
 * 确定性：给定相同输入，输出逐字节稳定。
 */
FFitResult fit_solve_trial(const FPoint *src, const FPoint *dst, int n,
                           const FBounds *bounds);

/*
 * 默认入口。默认调用线性方程组逻辑；用 -DFIT_NO_LINEAR 编译时整体回退到
 * 旧的逐个试逻辑（新逻辑关闭），供逐字节回归比对。
 */
FFitResult fit_solve(const FPoint *src, const FPoint *dst, int n,
                     const FBounds *bounds);

/* 把解出的变换作用到一个点上 */
FPoint fit_apply(FTransform tr, FPoint p);

/* 返回码的短名，供打印 */
const char *fit_status_name(int status);

#endif
