#ifndef SIMPLIFY_H
#define SIMPLIFY_H
#include "hull.h"

/* 面积保持型凸包顶点精简结果 */
typedef struct {
    HPoly poly;        /* 精简后的凸多边形（逆时针，严格凸） */
    int raw_count;     /* 输入点数 */
    int collinear;     /* 单独剔除的共线点数 */
    int hull_count;    /* 去共线、凸包提取后的严格凸包顶点数 */
    int est_count;     /* 解析式估计需要保留的顶点数 */
    int kept;          /* 实际保留的顶点数（== poly.count） */
    double area_full;  /* 严格凸包面积 A0 */
    double area_kept;  /* 精简后面积 */
    double ratio;      /* 实际面积保留比例 area_kept / area_full */
    double target;     /* 调用方要求的保留比例 */
} HSimp;

/* 返回码：
 *  0  成功
 * -1 参数非法（空指针 / n 超界 / 输出缓冲为空）
 * -2 保留比例不在 (0,1]
 * -3 输入点数不足 3
 * -4 严格凸包顶点少于 3（退化点集，拒绝精简）
 */
int hull_simplify(const HPoint *pts, int n, double ratio, HSimp *out);

/* 纯解析估计：给定凸包顶点数 n0 与目标面积保留比例 ratio，
 * 由正 n 边形面积比 1 - 2*pi^2/(3*n^2) 反解所需顶点数，
 * 夹在 [3, n0]。不做任何剔除。 */
int hull_estimate_keep(int n0, double ratio);
#endif
