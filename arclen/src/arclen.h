#ifndef ARCLEN_H
#define ARCLEN_H
typedef struct { double x, y; } APoint;
typedef struct { int count; double s[64]; } ATable;
int arclen_build(const APoint *pts, int n, ATable *out);
double arclen_total(const ATable *t, const APoint *pts, int n);
int arclen_sample_at(const APoint *pts, int n, const ATable *t, double s, APoint *out);
#endif
