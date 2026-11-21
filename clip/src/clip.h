#ifndef CLIP_H
#define CLIP_H
typedef struct { double x, y; } CPoint;
typedef struct { int count; CPoint pts[64]; } CPoly;
int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out);
double clip_area(const CPoly *p);
#endif
