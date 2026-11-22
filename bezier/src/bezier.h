#ifndef BEZIER_H
#define BEZIER_H

typedef struct { double x, y; } BPoint;
typedef struct { int count; BPoint pts[64]; } BPoly;

BPoint bezier_eval(const BPoint c[4], double t);
int bezier_flatten(const BPoint c[4], double tol, BPoly *out);
double bezier_chord(const BPoint c[4]);

/*
 * 自适应细分（新逻辑）
 * -------------------
 * 弦高误差上界：对一段三次贝塞尔及其端点弦，
 *
 *     max_{t} dist(B(t), chord) <= (3/4) * max(|P0 - 2P1 + P2|,
 *                                               |P1 - 2P2 + P3|)
 *
 * 因此要求弦高误差 <= E 时，每段二阶差分必须满足
 * (3/4)*m <= T，反解出最大允许容差 T = E / (3/4) = 4E/3。
 * 流程分两步，与“反解容差、再按容差细分并验证”一一对应：
 *
 *   1) bezier_tol_for_error(E)       反解最大允许容差 T（向下取 1 ulp，
 *                                      保证 (3/4)*T <= E 在浮点下成立）
 *   2) bezier_flatten_tol(..., T)    按容差 T 用 de Casteljau 递归细分
 *   3) bezier_verify_path(..., E)    逐段量测真实弦高误差并判定
 *
 * bezier_flatten_path() 是 1+2 的组合入口，内部记录反解出的 T。
 * 输出折线的每一段都带参数区间与来源段号，可逐段验证。
 *
 * 旧的 bezier_flatten() 保持固定 8 段、与容差无关，逐字节不变；
 * 新逻辑只在下列新入口中启用。BEZ_MODE_LEGACY_FIXED 是显式的
 * “关掉新逻辑”开关，用于产图逐字节比对。
 */

#define BEZ_MAX_PIECES    512  /* 单条路径最多输出折线段数 */
#define BEZ_MAX_PATH_SEGS  16  /* 单次调用最多输入三次段数 */
#define BEZ_CHORD_BOUND  0.75  /* 弦高误差 / 二阶差分 上界常数 */

enum {
    BEZ_OK = 0,
    BEZ_E_ARG = -1,       /* 空指针 / 段数越界 / 采样数非法 */
    BEZ_E_TOL = -2,       /* 容差为负、零或非有限；bad_seg = -1（全局参数） */
    BEZ_E_DEGEN = -3,     /* 控制点退化；bad_seg = 出错段号（0 基） */
    BEZ_E_CAPACITY = -4,  /* 输出超过 BEZ_MAX_PIECES；bad_seg = 出错段号 */
    BEZ_E_DEPTH = -5      /* 递归深度耗尽（数值病态）；bad_seg = 出错段号 */
};

/* 每段折线对应的参数区间（局部 t，属于其来源三次段）与来源段号。 */
typedef struct {
    int pieces;
    BPoint pts[BEZ_MAX_PIECES + 1];
    double t0[BEZ_MAX_PIECES];
    double t1[BEZ_MAX_PIECES];
    int seg[BEZ_MAX_PIECES];
} BFlat;

typedef struct {
    int nsegs;
    int pieces;
    double err_bound;        /* 要求的弦高误差上界 E */
    double tol;              /* 实际使用的容差（自适应时即反解值 4E/3） */
    double max_err;          /* 逐段量测到的最大弦高误差 */
    int pass;                /* 每一段量测误差都 <= err_bound */
    int violations;          /* 超界段数 */
    int bad_seg;             /* 拒绝时的出错段号；-1 表示全局参数 */
    int worst_seg;           /* 误差最大的折线所属输入段号 */
    int worst_piece;         /* 误差最大的折线在输出中的全局序号 */
    int worst_piece_in_seg;  /* 同一折线在其来源段内的序号 */
} BPathResult;

typedef enum { BEZ_MODE_LEGACY_FIXED = 0, BEZ_MODE_ADAPTIVE = 1 } BMode;

/* 由弦高误差上界反解最大允许容差：4E/3（向下取 1 ulp）。
 * E <= 0、非有限或溢出时返回 -1。 */
double bezier_tol_for_error(double err_bound);

/* 按给定容差（通常来自 bezier_tol_for_error）自适应细分一条由 nsegs 段
 * 三次贝塞尔首尾相接组成的路径。tol <= 0、非有限或段数越界时拒绝。
 * res->tol 记录本次使用的容差，res->err_bound = (3/4)*tol。 */
int bezier_flatten_tol(const BPoint (*segs)[4], int nsegs, double tol,
                       BFlat *out, BPathResult *res);

/* 组合入口：先由 err_bound 反解容差（bezier_tol_for_error），再调用
 * bezier_flatten_tol。res->err_bound 保留调用方要求的 E。 */
int bezier_flatten_path(const BPoint (*segs)[4], int nsegs, double err_bound,
                        BFlat *out, BPathResult *res);

/* 显式模式开关：LEGACY_FIXED 复刻旧固定 8 段（与 bezier_flatten 位级一致），
 * ADAPTIVE 等价于单段路径的 bezier_flatten_path。 */
int bezier_flatten_mode(const BPoint c[4], double tol_or_err, BMode mode,
                        BFlat *out, BPathResult *res);

/* 量测一段折线与其参数区间 [t0,t1] 上曲线之间的最大距离。
 * samples 为初采样数，随后在峰值附近做黄金分割细化。 */
double bezier_piece_error(const BPoint c[4], double t0, double t1,
                          BPoint a, BPoint b, int samples);

/* 逐段量测并判定：任一段误差 > err_bound 即 res->pass = 0。 */
int bezier_verify_path(const BPoint (*segs)[4], int nsegs, const BFlat *f,
                       double err_bound, int samples, BPathResult *res);

const char *bezier_strerror(int code);

#endif
