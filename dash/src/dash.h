#ifndef DASH_H
#define DASH_H
typedef struct { double x, y; } DPoint;
typedef struct { int count; DPoint pts[128]; } DPoly;
int dash_polyline(const DPoint *pts, int n, double on, double off, DPoly *out);
#endif
