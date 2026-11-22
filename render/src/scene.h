#ifndef RSCENE_H
#define RSCENE_H
typedef struct { double x, y; } RPoint;
typedef struct { int n; RPoint pts[64]; } RPoly;
typedef struct { int npolys; RPoly polys[32]; } RScene;
typedef struct { double minx, miny, maxx, maxy; } RBox;
void scene_init(RScene *s);
int scene_add_poly(RScene *s, const RPoint *pts, int n);
int scene_bbox(const RScene *s, RBox *out);
int scene_count_verts(const RScene *s);
#endif
