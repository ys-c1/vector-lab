#ifndef ISECT_H
#define ISECT_H
typedef struct { double x, y; } IPoint;
typedef struct { int count; IPoint pts[128]; } IPoly;
typedef struct { double t; double u; IPoint at; } IHit;
int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out);
int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits);
int isect_self(const IPoint *p, int n, int *first_a, int *first_b);

/* 容差扩展求交：先把两条段的端点各自沿段方向向外扩 tol（像素），
 * 再在扩过的区间上求交。tol == 0 时与 isect_seg 完全一致。
 * 命中时 out->t / out->u 已换算回原始段参数，故端点外延接触
 * 会落在 [0,1] 之外（可据此识别是扩展补上的接触）。
 * 返回 1 命中，0 未命中；
 *      -1 容差为负；
 *      -2 段 a 长度为零（无法确定扩展方向）；
 *      -3 段 b 长度为零。 */
int isect_seg_tol(IPoint a0, IPoint a1, IPoint b0, IPoint b1, double tol, IHit *out);

/* 折线版容差求交。tol == 0 时与 isect_polyline 完全一致。
 * 返回 0 正常（*hits 为命中数）；
 *      -1 参数非法；
 *      -2 容差为负；
 *      -3 折线 a 中段长为零，*err_seg 为该段段号；
 *      -4 折线 b 中段长为零，*err_seg 为该段段号。
 * err_seg 可为 NULL。 */
int isect_polyline_tol(const IPoint *a, int na, const IPoint *b, int nb,
                       double tol, int *hits, int *err_seg);
#endif
