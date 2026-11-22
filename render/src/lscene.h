#ifndef LSCENE_H
#define LSCENE_H
#include "scene.h"
#include "dag.h"

/* 图层场景文件（# 起始为注释，每行一条记录）：
 *   win  minx miny maxx maxy        可选，缺省取所有图层多边形的包围盒
 *   layer <名称> <灰度0-255> x1 y1 x2 y2 ...        至少 3 个顶点
 *   edge  <下层名> <上层名>          下层先渲染，上层遮挡下层
 *   order <名称> ...                 手排顺序（旧逻辑），必须列全且无重复
 */
typedef struct {
    char name[LDAG_NAME];
    unsigned char shade;
    RPoly poly;
} LLayer;

typedef struct {
    int n;
    LLayer layers[LDAG_MAX];
    LDag dag;
    int has_win;
    RBox win;
    int order_n;
    int order[LDAG_MAX];
} LScene;

void lscene_init(LScene *s);
/* 解析文件；失败返回非 0 并在 err 中给出原因。 */
int lscene_load(const char *path, LScene *out, char *err, int errlen);

/* 按给定顺序（图层下标数组）做 painter 覆盖渲染：
 * 单点中心采样，命中多边形即覆写为该图层灰度，后渲染者遮挡先渲染者。
 * 窗口取 s->win（缺省取全部图层包围盒）。 */
int lscene_render(const LScene *s, const int *order, int order_n,
                  int w, int h, unsigned char *buf);

/* 两个 PGM（P5，同尺寸）逐像素比较，返回不同像素数；头部不匹配或文件缺失返回 -1。 */
int lscene_pgm_diff(const char *a, const char *b, long *na, long *nb);

#endif
