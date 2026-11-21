#ifndef STROKE_H
#define STROKE_H
typedef struct { double x, y; } SPoint;
typedef struct { int count; SPoint pts[128]; } SPoly;
int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out);
#endif
