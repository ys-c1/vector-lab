#ifndef FIT_H
#define FIT_H

/* 最小二乘反解三次贝塞尔控制点：让曲线穿过美术给的必经点。
 * 与 bezier 模块（手拖控制点）相互独立，不影响既有产物。 */

#define FIT_MAX 512      /* 单组必经点上限 */
#define FIT_MAX_SEG 32   /* 分段样条的段数上限 */
#define FIT_TOL 0.5      /* 残差限差：半个像素 */

typedef struct { double x, y; } FPoint;

typedef struct {
    FPoint ctrl[4];   /* 三次贝塞尔控制点 P0..P3 */
    double max_err;   /* 本段最大残差（与输入同单位，像素） */
    int degenerate;   /* 0=正常拟合 1=共线退化（直线化） 2=全部重合 */
} FFit;

/* 单段拟合：由 pts[0..n-1] 反解一条三次贝塞尔的控制点。
 * 返回  0 成功；-1 点位少于 2，拒绝；-2 参数非法。 */
int fit_bezier(const FPoint *pts, int n, FFit *out);

/* 分段拟合：单段残差超 tol 时在最差点处一分为二，递归拟合，
 * 保证每段残差都不超 tol。段数写入 segs[0..retval-1]。
 * 返回  段数（>=1）；-1 点位少于 2；-2 参数非法；-3 段数超上限。 */
int fit_spline(const FPoint *pts, int n, double tol, FFit *segs, int max_segs);

#endif
