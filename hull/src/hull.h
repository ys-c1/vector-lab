#ifndef HULL_H
#define HULL_H
typedef struct { double x, y; } HPoint;
typedef struct { int count; HPoint pts[64]; } HPoly;
int hull_convex(const HPoint *pts, int n, HPoly *out);
int hull_min_rect(const HPoint *pts, int n, double *cx, double *cy, double *w, double *h, double *angle);
#endif
