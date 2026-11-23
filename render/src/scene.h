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
/* 文本场景：每行一个多边形，行内成对 "x y"，至少 3 点。
 * 返回多边形数；失败返回负值（-1 打开失败，-2 字段/容量错误）。 */
int scene_load_txt(const char *path, RScene *s);
#endif
