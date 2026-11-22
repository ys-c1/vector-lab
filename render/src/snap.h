#ifndef SNAP_H
#define SNAP_H
#include "scene.h"

/* 量化步长反解：把顶点吸附到像素网格，同时把面积漂移压进预算。
 *
 * 网格锚定在 (ox, oy)，x/y 步长独立（像素通常非正方）。
 * 候选阶梯（从粗到细）：
 *   hx = px * m,  m = max_m .. 1     粗侧，m = 1 即一个像素
 *   hx = px / m,  m = 2 .. max_m     细侧，亚像素
 * 取“误差预算内最粗”的步长：它吸附最强，而更细的步长误差都趋于 0，
 * 单看“误差最小”会退化成 h->0 的平凡解，所以用预算作约束、取最粗可行。
 * 任何使图形塌陷的步长一律拒收，并记录首个拒收样本作为说明。
 */
#define SNAP_MAX_POLY 32

typedef enum {
    SNAP_OK = 0,          /* 面积符号不变、非退化 */
    SNAP_INPUT_DEGEN = 1, /* 输入本身面积为零：单独报告，不计入误差 */
    SNAP_DEG_POINT = 2,   /* 全部顶点落进同一格 -> 退化成点 */
    SNAP_DEG_SEGMENT = 3, /* 只有两个不同格 -> 退化成线段 */
    SNAP_DEG_ZERO = 4,    /* >=3 个格但带符号面积为零 */
    SNAP_DEG_FLIP = 5,    /* 面积符号翻转（绕向变了） */
    SNAP_DEG_GROW = 6     /* 输入退化的图形吸附后反而长出面积 */
} SnapStatus;

typedef struct {
    double hx, hy;   /* 实际步长 */
    double px, py;   /* 基础步长（一个输出像素） */
    int coarse;      /* 1: h = p*m；0: h = p/m */
    int m;           /* 阶梯序号 */
    double area0;    /* 吸附前面积：各多边形 |带符号面积| 之和 */
    double area1;    /* 吸附后面积 */
    double err;      /* |area1-area0| / area0 */
    double worst_err;/* 单多边形最大相对误差 */
    int worst_poly;  /* 该多边形下标，无则 -1 */
    int npolys;
    unsigned char status[SNAP_MAX_POLY];
    unsigned char input_kind[SNAP_MAX_POLY]; /* 0 正常, 1 点, 2 共线, 3 零面积 */
    int first_bad;   /* 首个塌陷多边形下标，无则 -1 */
} SnapEval;

/* 单个多边形在候选阶梯上的塌陷实测区间（塌陷对 h 非单调）。 */
typedef struct {
    int count;      /* 出现塌陷的候选数 */
    int point_count;/* 其中退化成点的候选数 */
    double min_hx, min_hy, max_hx, max_hy;         /* 任一种塌陷的步长区间 */
    double point_min_hx, point_min_hy, point_max_hx, point_max_hy; /* 塌成点 */
    int finest_status;  /* 最细那次塌陷的类型 */
} SnapCollapse;

typedef struct {
    SnapEval chosen;
    int rc;               /* 0: 命中预算；1: 无预算内解；2: 全部塌陷 */
    int evaluated;        /* 实际评估的候选数 */
    double rejected_hx;   /* 最细的塌陷样本（仍会塌陷的最小步长） */
    double rejected_hy;
    int rejected_poly;    /* 及塌陷的多边形/类型 */
    int rejected_status;
    int have_rejected;
    SnapCollapse collapse[SNAP_MAX_POLY];
    int all_input_degenerate;
} SnapSolve;

#define SNAP_MAX_ROWS 128
typedef struct { int n; SnapEval rows[SNAP_MAX_ROWS]; } SnapLadder;

int snap_ladder(const RScene *s, double px, double py, double ox, double oy,
                int max_m, SnapLadder *lad);
int snap_input_kind(const RPoly *p);
int snap_eval(const RScene *s, double px, double py, double ox, double oy,
              int coarse, int m, SnapEval *ev, RScene *out);
int snap_solve(const RScene *s, double px, double py, double ox, double oy,
               double tol, int max_m, SnapSolve *sol, RScene *out);
const char *snap_status_name(int status);
#endif
