#ifndef CLIP_H
#define CLIP_H
#include <stddef.h>
typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[64]; } CPoly;

/* 0 成功；-1 参数/顶点数非法；-2 窗口退化；-3 内存不足或结果超 64 点 */
int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);
double clip_area(const CPoly *p);

/* 单步裁剪的工作内存（字节），n 为进入该步的多边形顶点数 */
size_t clip_working_bytes(int n);
#endif
