#ifndef ISECT_H
#define ISECT_H
typedef struct { double x, y; } IPoint;
typedef struct { int count; IPoint pts[128]; } IPoly;
typedef struct { double t; double u; IPoint at; } IHit;
int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out);
int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits);
int isect_self(const IPoint *p, int n, int *first_a, int *first_b);
#endif
