#ifndef ADAPTIVE_H
#define ADAPTIVE_H

#include <stdio.h>

/*
 * 按曲率分配切段：给定总偏差上界 eps，给每一段曲线/线段分配切刀数，
 * 使每段的实际偏差都不超过 eps，且总切刀数最少。
 *
 * 形状由若干“段”(piece) 组成，每段有两种：
 *   AD_PIECE_LINE  直线段，长度 len，曲率恒为 0；
 *   AD_PIECE_ARC   圆弧段，弧长 len，半径 R>0，曲率 k=1/R 恒定。
 * 直线段任何切法偏差都是 0，最少只需 1 段（即不切）。
 *
 * 分配公式（本模块的全部依据）：
 *   把一段按弧长等分成 n 份，步长 h = len/n。等分模型（份角足够小，
 *   弓高 cos 展开到二次）下每份偏差
 *       dev_n = k*h^2/8 = k*(len/n)^2/8              (1)
 *   偏差相等 => h^2 ∝ 1/k，即步长平方与曲率成反比（“曲率平方的反比”
 *   说的是步长平方）；于是份数
 *       n ∝ len*sqrt(k)，k=1/R 时 n ∝ len/sqrt(R)。
 *   要求 dev_n <= eps 得连续下界
 *       n >= c = len*sqrt(k/(8 eps))                 (2)
 *   拉格朗日：min sum n_i  s.t. sum_i k_i (len_i/n_i)^2/8 = eps
 *       L = sum n_i + mu*(sum_i ... - eps)
 *       dL/dn_i = 0  =>  n_i = len_i*sqrt(k_i/(8 eps*mu))    (3)
 *   即最优时各段份数与 len_i*sqrt(k_i) 成正比、偏差都取等号。
 *   但题目要求“任何一段超上界就算没过”，逐段硬约束的最小整数解是
 *   每段各自取等号的下界 (2) 再向上取整：
 *       N_min = sum_i max(1, ceil(c_i))              (4)
 *   任何 n_i < ceil(c_i) 都会让该段超上界，故 (4) 是整数最优；
 *   逐段反解 n_i = max(1, ceil(c_i)) 与 (3) 同形（mu=1），这也是本模块
 *   实际落盘的分配。模型只用于求下界，最终每段都用精确弓高
 *   R*(1-cos(len/(2nR))) 复核，若仍超上界就再上调一份（下界取整的兜底）。
 *
 * 均匀分配基线（同样上界，用于“少两成”对比）：
 *   uniform_per_piece = max_i n_i^exact，uniform_total = 其值 * npieces
 *   n_i^exact 为圆弧的精确最少份数（见 adaptive_piece_exact_n），直线为 1。
 *   即每段都给同样的段数，取能同时满足所有段的最小那个——这是对均匀
 *   分配最有利的取法（比按等弧长均分更省段数），仍要被自适应明显超过。
 *
 * 参照点与落盘：每段按弧长等分成 n_i 份，切点首尾相接写到点文件
 *   （首行点数，随后每行 "%.6f %.6f"，与 gold/ref points 同格式）。
 *   落盘后逐段交给 gold/ref points 对同一段加密折线再等分一次，
 *   用 tools/cmp_points 逐点比，验证“等分落盘”与独立参照一致；
 *   见 adaptive/demo.c。
 */

#define AD_MAX_PIECES 256
#define AD_FIELD_MAX  32

enum { AD_PIECE_LINE = 0, AD_PIECE_ARC = 1 };

typedef struct { double x, y; } AdPoint;

typedef struct {
    int    kind;   /* AD_PIECE_LINE / AD_PIECE_ARC */
    double len;    /* 弧长，必须 > 0 */
    double R;      /* 圆弧半径；直线段忽略（须为 0）*/
} AdPiece;

/* 一段分配结果 */
typedef struct {
    int    piece;      /* 段号（输入顺序）*/
    int    kind;
    double len;
    double R;
    double kappa;      /* 曲率 k = 1/R；直线为 0 */
    double n_cont;     /* 连续下界 c_i（式 (2)）*/
    int    n;          /* 分配段数 max(1, ceil(c_i)) */
    double dev_model;  /* 模型偏差 k*(len/n)^2/8（式 (1)）*/
    double dev_exact;  /* 逐份实测偏差：圆弧用弓高精确式，直线为 0 */
    double dev_measure;/* 对圆弧，用加密采样实测的偏差（仅供参考）*/
    int    is_curve;   /* kind == AD_PIECE_ARC */
} AdAlloc;

typedef struct {
    double eps;                   /* 总偏差上界 */
    int    npieces;
    int    n_total;               /* sum n_i */
    int    n_curve;               /* 圆弧段的 sum n_i */
    int    n_line;                /* 直线段的 sum n_i */
    int    uniform_per_piece;     /* 均匀基线：每段同一 n */
    int    uniform_total;
    AdAlloc a[AD_MAX_PIECES];
} AdPlan;

enum {
    AD_OK = 0,
    AD_E_ARG = -1,      /* 空指针 / 段数越界；field = "pieces"/"np"/"out" */
    AD_E_EPS = -2,      /* eps 非正或非有限；field = "eps" */
    AD_E_LEN = -3,      /* 长度非正或非有限；field = "len" */
    AD_E_RADIUS = -4,   /* 圆弧半径非正/非有限，或直线段给了非零半径；field = "R" */
    AD_E_KIND = -5,     /* 未知段类型；field = "kind" */
    AD_E_CAP = -6       /* 总段数超过 int / 输出容量；field = "n" */
};

/* 规划：校验输入、按式 (2)~(4) 逐段反解切刀数，并给出均匀基线。
 * 失败返回负码，err_piece 为出错段号（-1 表示整体参数）。 */
int  adaptive_plan(const AdPiece *pieces, int np, double eps,
                   AdPlan *out, int *err_piece, char err_field[AD_FIELD_MAX]);

/* 连续下界 c_i = len*sqrt(k/(8 eps))（式 (2)）；直线段或非法参数返回 0。 */
double adaptive_piece_cont(double len, double R, double eps);

/* 精确最少份数：对圆弧按圆心角用 arccos 反解 n，使精确弓高
 *   R*(1-cos(d/2)) <= eps，再对整数上下微调直到成立。
 * 该基线同时用于“均匀分配”和“每段实际偏差”的精确核对。 */
int adaptive_piece_exact_n(double len, double R, double eps);

/* 把规划落成段内等分的切点：每段先取起点（第一段才输出）再输出 n_i 个
 * 切点，共 sum n_i + 1 个点；段内按该段的参数方向前进。
 * shape 回调负责把 (段号, 段内弧长 s∈[0,len]) 映射成坐标。 */
typedef AdPoint (*AdShapeFn)(int piece, double s, void *ctx);
int adaptive_plan_points(const AdPlan *plan, AdShapeFn shape, void *ctx,
                         AdPoint *out, int cap);

/* 均匀基线的点：每段都切成 uniform_per_piece 份。 */
int adaptive_uniform_points(const AdPlan *plan, AdShapeFn shape, void *ctx,
                            AdPoint *out, int cap);

/* 点文件读写：首行点数，随后每行 "%.6f %.6f"（与 gold/ref points 同格式，
 * 便于逐字节比对）。失败返回负错误码。 */
int adaptive_points_save(const char *path, const AdPoint *p, int n);
int adaptive_points_load(const char *path, AdPoint **out, int *n);

/* 圆弧段实测偏差：在每一份里取足够密的中点样本，量到该份弦的距离。
 * 返回最大偏差；直线段返回 0。 */
double adaptive_arc_dev_measure(double R, double len, int n, int samples);

const char *adaptive_strerror(int code);

#endif
