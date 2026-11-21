#ifndef PIP_H
#define PIP_H
#include <stdio.h>

#define PIP_MAX_POLY 128

typedef struct { double x, y; } PPoint;

enum {
    PIP_OUTSIDE = 0,
    PIP_INSIDE = 1,
    PIP_UNDECIDED = 2   /* 未启用时的判定结果 */
};

typedef struct {
    int result;              /* PIP_INSIDE / PIP_OUTSIDE / PIP_UNDECIDED */
    int winding;             /* 绕数 */
    int on_edge;             /* 点在边上：结果随机判内判外 */
    int n_edges;             /* 贡献越边的数量 */
    int edges[PIP_MAX_POLY]; /* 贡献越边的边号，逐点列出 */
} PIPVerdict;

/* 有向面积：>0 逆时针，<0 顺时针，0 为退化多边形 */
double pip_signed_area(const PPoint *poly, int n);

/* 单点判定。返回 0 成功；-1 输入非法；-2 退化多边形（零面积） */
int pip_point(const PPoint *poly, int n, PPoint p, PIPVerdict *out);

/* 批量判定。enabled=0 时不做几何计算，全部返回 PIP_UNDECIDED。
 * 返回 0 成功；失败时 *err_at 指出序号：
 *   点数为零（m<=0 或 pts 为空） -> *err_at = 0
 *   多边形顶点不足（n<3）        -> *err_at = n（第一个缺失顶点的序号）
 *   其它空指针                   -> *err_at = -1
 * 返回 -2 表示退化多边形（零面积）。 */
int pip_batch(const PPoint *poly, int n, const PPoint *pts, int m,
              int enabled, PIPVerdict *out, int *err_at);

/* 导出判定表（CSV）：逐点列出边号与绕数 */
int pip_table_csv(FILE *fp, const PPoint *pts, const PIPVerdict *v, int m);

#endif
