#ifndef STROKE_AREA_H
#define STROKE_AREA_H

/* 按目标覆盖面积反解线宽。
 *
 * 开启方式：编译时定义 STROKE_AREA_FIT（`make -C stroke demo-area` /
 * `test-area` 已带该开关）。未定义时本头文件不声明任何东西，
 * stroke_area.c 是空编译单元，旧固定宽度路径完全不受影响。
 *
 * 面积模型
 * --------
 * 把描边看成中心线 ⊕ 半径 h=width/2 的圆盘（即每段一个胶囊体的并集）。
 * 对不自交的中心线，覆盖面积对宽度的累积积分为
 *
 *     A(w) = ∫₀^w [ L + (cap - C)·t/2 ] dt
 *          = L·w + (cap - C)·w²/4
 *
 *   L   中心线总长
 *   cap 开放路径为 π（两端各一个半圆），闭合路径为 0
 *   C   Σ_顶点 (tan(φ/2) - φ/2)，φ 为无符号转角，减去拐角处重复计入的
 *       圆盘扇形；开放路径只计内部顶点，闭合路径首尾都计
 *
 * 反解即解一元二次方程 L·w + k·w² = target（k = (cap-C)/4），取面积随
 * 宽度递增的那一支。模型在带宽互相重叠后（大宽度、自交中心线、接近
 * 180° 的折返、细长闭合环）不再成立。程序先用模型反解，再用独立的
 * 逐行胶囊并集实测复核：偏差超过 0.5% 就改用实测面积做二分反解，并在
 * 结果里标明 `used_measurement`。
 */

#include "stroke.h"

#define STROKE_AREA_MAX_PTS 256

/* 返回值 */
#define STROKE_AREA_OK            0
#define STROKE_AREA_E_ARG        -1  /* 指针为空 / 点数超出上限            */
#define STROKE_AREA_E_WIDTH      -2  /* 宽度为负或宽度上下界非法            */
#define STROKE_AREA_E_ZERO_LEN   -3  /* 路径长度为零                        */
#define STROKE_AREA_E_TARGET     -4  /* 目标面积非正或非有限                */
#define STROKE_AREA_E_NO_SOLUTION -5 /* 在给定宽度范围内无法达到目标面积    */
#define STROKE_AREA_E_CONVERGE   -6  /* 实测反解未能在迭代上限内收敛        */

typedef struct {
    double width;        /* 反解出的宽度                                   */
    double coverage;     /* 该宽度下的实测覆盖面积（胶囊并集）             */
    double target;       /* 目标面积                                       */
    double rel_err;      /* |coverage - target| / target                   */
    double length;       /* 中心线总长                                     */
    double quad;         /* 二次项系数 k = (cap - C)/4                     */
    double model_width;  /* 二次模型反解出的宽度；模型不适用时为 -1        */
    double model_area;   /* 该宽度下的模型面积；模型不适用时为 -1          */
    double ms;           /* 反解耗时（毫秒）                               */
    int    closed;       /* 1 = 闭合路径                                   */
    int    self_intersecting; /* 1 = 中心线自交，已单独走实测分支         */
    int    self_seg_a;   /* 首个自交的段号（无自交为 -1）                  */
    int    self_seg_b;   /* 与它相交的段号                                 */
    double self_x;       /* 首个自交点坐标                                 */
    double self_y;
    int    used_measurement; /* 1 = 模型不可用/偏差过大，宽度由实测反解    */
    int    iterations;   /* 实测二分迭代次数（模型直接命中为 0）           */
} SAreaFit;

/* 反解使覆盖面积达到 target_area 的宽度。
 *   closed     1 表示 pts 是闭合环（首尾自动相连；无需重复首点）
 *   w_min/w_max 允许的宽度范围，均须 >= 0 且 w_max > w_min
 * 返回 STROKE_AREA_OK 或上面任一错误码；错误码文字见 strerror。
 * fit 中除 ms 外的字段均为输出。 */
int stroke_area_fit(const SPoint *pts, int n, int closed, double target_area,
                    double w_min, double w_max, SAreaFit *out);

/* 独立量一次覆盖面积（逐行胶囊并集 + 自适应 Simpson 积分），供核对/测试用。
 * 积分在圆弧端头的切点处切分区间，再按自适应 Simpson 细分，小宽度和大
 * 宽度都能达到相对 1e-9 量级精度。
 *   rel_tol  自适应积分相对精度；<=0 取默认 1e-9，越小越准（下限 1e-12）。
 * 参数非法时返回 -1.0。 */
double stroke_area_measure(const SPoint *pts, int n, int closed,
                           double width, double rel_tol);

/* 二次模型面积；模型不适用（自交/折返）时返回 -1.0。
 * length/quad 可为 NULL；非空时回填 L 与 k。 */
double stroke_area_model(const SPoint *pts, int n, int closed, double width,
                         double *length, double *quad);

const char *stroke_area_strerror(int code);

#endif /* STROKE_AREA_H */
