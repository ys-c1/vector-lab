#ifndef BBOX_H
#define BBOX_H
typedef struct { double x, y; } GPoint;
typedef struct { double minx, miny, maxx, maxy; } GBox;
int bbox_of_points(const GPoint *pts, int n, GBox *out);
int bbox_union(const GBox *a, const GBox *b, GBox *out);
double bbox_area(const GBox *b);
#endif
