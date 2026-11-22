#ifndef STROKE_ADAPTIVE_H
#define STROKE_ADAPTIVE_H
#include "stroke.h"

#define STROKE_MAX_SEG 127

/* 段曲率标记：零曲率（直线段）与无穷曲率（零长边或 180° 回折）单独标出 */
enum { SCURV_OK = 0, SCURV_ZERO = 1, SCURV_INF = 2 };

typedef struct {
    double kappa;     /* 段曲率（带符号）；无穷段为 HUGE_VAL */
    double width;     /* 反解并夹取后的段宽 */
    double deviation; /* 实际视觉粗细相对目标的偏差；无穷段为 NAN */
    int flag;         /* SCURV_* */
} SSegReport;

typedef struct {
    int nseg;
    SSegReport seg[STROKE_MAX_SEG];
    double min_width, max_width; /* 比值保证 <= 1.2 */
    double max_abs_deviation;
    double seconds;              /* 本路径求解耗时 */
} SWidthReport;

/* 逐段离散曲率：顶点转角 / 邻边均长。 */
int stroke_curvature(const SPoint *pts, int n, double *kappa, int *flags);

/* 由目标视觉宽度按曲率反解每段宽度；最粗/最细夹取到 1.2 以内。 */
int stroke_widths_solve(const SPoint *pts, int n, double target_width,
                        double *widths, SWidthReport *rep);

/* 变宽描边；widths[i] <= 0 拒绝，返回 -3 并经 err_seg 给出段号。 */
int stroke_polyline_varying(const SPoint *pts, int n, const double *widths,
                            SPoly *out, int *err_seg);

/* 统一入口：adaptive=0 时走原始等宽路径（输出逐字节不变）。 */
int stroke_outline(const SPoint *pts, int n, double width, int adaptive,
                   SPoly *out, SWidthReport *rep);
#endif
