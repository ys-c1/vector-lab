#ifndef FIT_H
#define FIT_H
/*
 * fit: 反解裁剪窗口顶点数。
 * 给定被裁多边形 subj 与目标窗口 target，枚举正 n 边形窗口的顶点数 n，
 * 对每个 n 旋转扫描取最小面积误差，按停止规则选定 n。
 * 自包含实现，不依赖 clip 模块（其 CPoly 容量仅 64，容纳不下 n≈80 的窗口）。
 */
#define FIT_MAX 320      /* 容纳 K=256 的参考圆窗口与 n<=96 的候选窗口 */
#define FIT_TOL 1e-3     /* 面积误差千分之一上限 */
#define FIT_EPS 1e-12    /* 数值意义上的零误差（精确地板） */
#define FIT_GAIN 1e-4    /* 连续相对增益小于此值视为改进枯竭 */

typedef struct { double x, y; } FPoint;
typedef struct { int n; FPoint pts[FIT_MAX]; } FPoly;

/* 基础图元：均自行校验，顶点不足返回负错误码 */
double fit_area(const FPoly *p);
int    fit_bbox(const FPoly *const *polys, int np, double box[4]);
int    fit_clip(const FPoly *subj, const FPoly *win, FPoly *out);
int    fit_regular(int n, double cx, double cy, double r, double theta, FPoly *out);
int    fit_collinear(const FPoly *p, int *idx, int cap);
int    fit_write_pgm(const char *path, const FPoly *const *polys, int np,
                     const double box[4], int w, int h, int ss);

/* 停止规则码 */
enum {
    FIT_STOP_EXACT = 0,  /* 误差到达零地板：顶点数与目标多边形匹配，再加点误差回升 */
    FIT_STOP_TOL   = 1,  /* 误差首次进入千分之一：光滑目标按 O(1/n^2) 收敛，继续只买亚阈值精度 */
    FIT_STOP_GAIN  = 2,  /* 增益地板：连续多步相对改进过小 */
    FIT_STOP_CAP   = 3   /* 触达顶点数上限仍未满足 */
};

typedef struct {
    int n;               /* 顶点数 */
    double err;          /* 该 n 下的最小相对面积误差 */
    double theta;        /* 取到最小误差时的朝向（弧度） */
    double area;         /* 该朝向下的裁剪面积 */
    int collinear;       /* 候选窗口上的共线顶点数（正多边形恒为 0） */
} FitRow;

typedef struct {
    int chosen;                  /* 选定顶点数 */
    double chosen_err;
    double chosen_theta;
    double tol;                  /* 本次求解采用的容差 */
    int stop_code;
    const char *stop_reason;
    int rows_n;
    FitRow rows[FIT_MAX];
    double target_area;
    double ms;                   /* 整组枚举+求解耗时（毫秒） */
} FitReport;

/*
 * 反解求解：tol 为面积误差上限（默认 FIT_TOL），
 * gain_eps 为相对增益枯竭阈值（默认 FIT_GAIN），传 <=0 用默认值。
 * 返回 0 成功；负数为错误：
 * -1 空指针；-2 被裁多边形/目标/nmax 顶点<3（拒绝）；-3 半径或窗口退化；
 * -4 nmax 超出容量；-5 ntheta 过小；-6 目标裁剪面积非正。
 */
int fit_solve(const FPoly *subj, const FPoly *target, int nmax, int ntheta,
              double tol, double gain_eps, FitReport *rep);

const char *fit_strerror(int code);
#endif
