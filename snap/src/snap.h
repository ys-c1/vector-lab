#ifndef SNAP_H
#define SNAP_H

/*
 * snap：反解等分总长，让等分点尽量落在整数坐标（像素格点）上。
 *
 * 折线按累计弧长等分：第 k 个点位于弧长 s_k = k*d，k = 0..m-1，
 * 目标总长 L = (m-1)*d，且 L <= 折线总长 T。默认的小数等分取
 * d = T/(m-1)（L = T），导出的点必然带小数，取整时误差逐点累积。
 * 这里把 d（也就是 L）当作未知量反解：在
 *     d ∈ [T/m, T/(m-1)]      （即 (m-1)/m * T <= L <= T）
 * 内搜索，使每个点 x/y 到最近整数的误差尽量小。区间下界保证最后一段
 * 不超过间距 d（不会多出第 m+1 个点），上界保证 L 不超过折线总长。
 *
 * 目标函数（字典序，越小越好）：
 *   1. 半像素违反量 max(0, max_err - 0.5)   —— 硬约束优先
 *   2. exact 标志                          —— 全部落在整点者优先
 *   3. 最大欧氏取整误差 max_err = max_k hypot(ex,ey)
 *   4. 均方根取整误差 rms_err
 *   5. -L                                  —— 同误差下总长取最长
 * 其中 ex = x - round(x)、ey = y - round(y)。
 *
 * 搜索：整数格点穿越给出的候选 d（k=1 的点必须落在格点上才可能 exact）
 *        加上折线顶点落在格点时给出的 s/k 候选，再做全区间粗网格 +
 *        多起点局部收缩；评价时按当前最优做早停剪枝。
 */

#define SNAP_HALF_PIXEL 0.5   /* 取整误差上限：半个像素（欧氏） */
#define SNAP_EXACT_EPS 1e-6   /* 认为"落在整点"的数值容差 */

typedef struct { double x, y; } SnapPoint;

typedef struct {
    double x, y;      /* 反解得到的等分点（小数坐标） */
    double rx, ry;    /* 导出用的取整坐标（整数，以 double 存放） */
    double ex, ey;    /* 取整误差 = 原值 - 取整值，∈ [-0.5, 0.5] */
    double err;       /* 欧氏取整误差 hypot(ex, ey) */
} SnapNode;

enum {
    SNAP_STATUS_EXACT = 0,      /* 所有点都落在整点上 */
    SNAP_STATUS_NEAREST = 1,    /* 无整点解，但最大误差在半像素内 */
    SNAP_STATUS_OVER_BUDGET = 2 /* 半像素内无解，给出最接近的方案 */
};

typedef struct {
    int m;                    /* 等分点数 */
    int status;               /* SNAP_STATUS_* */
    int exact;
    int feasible;             /* max_err <= SNAP_HALF_PIXEL */
    double total;             /* 折线总长 T */
    double target;            /* 反解出的目标总长 L = (m-1)*d */
    double step;              /* 等分间距 d */
    double cover;             /* L / T */
    double max_err;           /* max 欧氏取整误差 */
    double axis_err;          /* max(|ex|,|ey|) */
    double rms_err;           /* 均方根取整误差 */
    unsigned long long evals; /* 目标函数求值次数（诊断用） */
    char detail[256];         /* 拒绝/说明文本 */
} SnapResult;

enum {
    SNAP_OK = 0,
    SNAP_E_ARG = -1,        /* 参数非法（空指针、m<2、n<2） */
    SNAP_E_CLOSED = -2,     /* 起点与终点重合（闭合折线），拒绝 */
    SNAP_E_DEGENERATE = -3, /* 零长线段 / 非有限坐标 / 总长为零 */
    SNAP_E_CAP = -4,        /* 输出容量不足 */
    SNAP_E_NOMEM = -5       /* 内存不足 */
};

/*
 * 反解最优等分总长。
 * nodes 为 NULL 时只求解并填 out（查询模式）；否则 maxnodes 至少为 m，
 * 每个点的原始小数坐标与取整坐标一并写出。
 */
int snap_solve(const SnapPoint *pts, int n, int m, SnapResult *out,
               SnapNode *nodes, int maxnodes);

/*
 * 旧的小数等分（新逻辑关闭）：d = T/(count-1)，逐点反解与 gold/ref points
 * 完全相同的公式。输出格式一致时可逐字节比对。
 */
int snap_points_legacy(const SnapPoint *pts, int n, int count, SnapPoint *out);

const char *snap_status_str(int status);
const char *snap_strerror(int code);

#endif
