#ifndef FIT_H
#define FIT_H
/*
 * 最小面积旋转包围盒（旋转卡尺 / rotating calipers）。
 *
 * 点集 P 绕原点旋转 theta 后的轴对齐包围盒面积：
 *   w(theta) = max(x cosθ - y sinθ) - min(...)
 *   h(theta) = max(x sinθ + y cosθ) - min(...)
 *   A(theta) = w(theta) * h(theta)
 * A 以 pi/2 为周期，因此角度只需在 [0, pi/2) 内寻找。
 */
typedef struct { double x, y; } FPoint;

typedef struct {
    double angle;     /* 最优旋转角（弧度），落在 [0, pi/2) */
    double area;      /* 该角度下的轴对齐包围盒面积 */
    double area0;     /* theta = 0 时的包围盒面积 */
    double w, h;      /* 最优角度下包围盒的宽、高 */
    int    indifferent; /* 无差别场景（如正方形）：各角度面积无显著差异 */
} FitResult;

/* 计算最小面积包围盒角度。n < 1 或空指针返回 -1。 */
int fit_optimize(const FPoint *pts, int n, FitResult *out);

/* 角度合法性：只接受 [0, pi/2)。越界返回 -1，合法返回 0。 */
int fit_validate(double angle);

/* 指定角度下的包围盒面积（用于校验与复算）。 */
double fit_area_at(const FPoint *pts, int n, double angle);

#define FIT_EPS 1e-9
#endif
