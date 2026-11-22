#ifndef ISECT_H
#define ISECT_H
typedef struct { double x, y; } IPoint;
typedef struct { int count; IPoint pts[128]; } IPoly;
typedef struct { double t; double u; IPoint at; } IHit;

/* legacy 固定阈值路径（阈值 1e-12 写死）：保持冻结，不得改算法 */
int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out);
int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits);
int isect_self(const IPoint *p, int n, int *first_a, int *first_b);

/* 新路径：两线段间最小欧氏距离 gap（0 表示相交/接触/重叠） */
double isect_gap(IPoint a0, IPoint a1, IPoint b0, IPoint b1);
int isect_zero_len(IPoint a0, IPoint a1);
#endif
