#ifndef HOMOG_H
#define HOMOG_H

/*
 * 单应矩阵（透视映射）模块。
 *
 * H 把源点 src 映射到目标点 dst：
 *   X = (h11*x + h12*y + h13) / (h31*x + h32*y + 1)
 *   Y = (h21*x + h22*y + h23) / (h31*x + h32*y + 1)
 * 即固定 h33 = 1 的 8 自由度 3x3 单应。
 *
 * 典型用途：把一张贴图的四个角（单位正方形）反解到屏幕上的任意
 * 凸四边形，替代原先手调的仿射映射（仿射无法表达透视收缩，贴图会歪）。
 */

typedef struct { double x, y; } HPoint;

typedef struct {
    double h11, h12, h13;
    double h21, h22, h23;
    double h31, h32;          /* h33 固定为 1 */
} Homog;

typedef struct {
    double rms;               /* 四角均方根残差，单位像素 */
    double maxerr;            /* 四角最大残差，单位像素 */
    double perr[4];           /* 每个角的逐点残差，按入参顺序 */
} HResidual;

/*
 * 全局开关：0 = 关闭新逻辑，homog_solve 退化为最小二乘仿射
 * （h31 = h32 = 0），与旧的手调仿射路径逐字节一致；
 * 1（默认）= 透视单应。可在运行时任意切换。
 */
void homog_set_perspective(int on);
int  homog_perspective_enabled(void);

HPoint homog_apply(Homog h, HPoint p);
Homog  homog_identity(void);

/* 求逆（贴图反向采样用）：out 满足 apply(h, apply(out, p)) == p。
 * 行列式过近 0 时返回 -8，不写 *out。 */
int homog_invert(const Homog *h, Homog *out);

/* 预处理校验：返回 0 表示可用，负值表示退化。
 *  - -6：角点重合，*bad_pair 输出重合的那一对下标 (i,j)
 *  - -7：四点共线，*bad_pair 输出面积最小的三点中的一对
 * 其它负值：-1 空指针，-5 点数不足 4。 */
int homog_check(const HPoint *dst, int n, int *bad_pair);

/* 用四对点解单应并做最小二乘精化。
 * src/dst 各 4 个点，按角点顺序对应。
 * 成功返回 0 并填充 *out 与 *res（res 可传 NULL）；
 * 退化时返回 homog_check 的负值且不写 *out；
 * 精化后角点最大残差仍超过 0.5px 返回 -9。 */
int homog_solve(const HPoint *src, const HPoint *dst, Homog *out, HResidual *res);

/* 只做初始 DLT 解，不做精化（测试/对照用）。 */
int homog_solve_dlt(const HPoint *src, const HPoint *dst, Homog *out);

/* 在初值上做高斯-牛顿最小二乘，压低四角重投影误差。返回 0。 */
int homog_refine(const HPoint *src, const HPoint *dst, Homog *h, HResidual *res);

/* 仿射路径（开关关闭时使用）：6 参数最小二乘，h31 = h32 = 0。
 * 四点不共线即可，角点重合仍拒绝。 */
int homog_solve_affine(const HPoint *src, const HPoint *dst, Homog *out, HResidual *res);

/* 计算给定单应在四对上的残差。 */
void homog_residual(const HPoint *src, const HPoint *dst, const Homog *h, HResidual *res);

#endif
