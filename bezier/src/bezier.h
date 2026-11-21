#ifndef BEZIER_H
#define BEZIER_H
typedef struct { double x, y; } BPoint;
typedef struct { int count; BPoint pts[64]; } BPoly;
BPoint bezier_eval(const BPoint c[4], double t);
int bezier_flatten(const BPoint c[4], double tol, BPoly *out);
double bezier_chord(const BPoint c[4]);
#endif
