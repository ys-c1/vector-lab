#ifndef ZORDER_H
#define ZORDER_H

/* 反解绘制顺序：按包围盒重叠关系排出画家顺序，再验证遮挡结果。
 *
 * 输入：图形集合（添加顺序）、每形灰度、目标图（期望的遮挡结果，
 * 须由这些图形按某个一致的层序逐子采样合成，颜色两两不同）。
 * 输出：绘制顺序（先画在前）。包围盒正面积重叠的图形对产生约束，
 * 约束方向由目标图中被两形完全覆盖的像素颜色反解；零面积接触
 * 单独统计、不产生约束；约束成环时拒绝并指出环上图形。 */

#define ZO_MAX 32   /* 图形上限 */
#define ZO_MAXV 64  /* 每形顶点上限 */

typedef struct { double x, y; } ZPoint;
typedef struct { int n; ZPoint pts[ZO_MAXV]; } ZPoly;
typedef struct { double minx, miny, maxx, maxy; } ZBox;

typedef struct {
    int pairs;      /* 正面积重叠对数（可能产生约束） */
    int zero_area;  /* 面积为零的接触对数（单独统计，不产生约束） */
    int edges;      /* 目标图证据产生的约束边数 */
    int undecided;  /* 重叠但目标图无可判定证据的对数 */
} ZStats;

int zorder_poly_bbox(const ZPoly *p, ZBox *out);

/* 反解画家顺序。
 * order: 输出绘制顺序（图形下标，先画在前），容量 >= n。
 * 返回 0 成功；-1 参数错误；-2 环依赖（cyc/ncyc 带回环上图形下标，可为 NULL）。 */
int zorder_solve(const ZPoly *polys, const unsigned char *colors, int n,
                 const unsigned char *target, ZBox win, int w, int h, int ss,
                 int *order, ZStats *stats, int *cyc, int *ncyc);

/* 按给定顺序画家法合成：每个子采样点取顺序最靠后的覆盖图形颜色，按 ss*ss 平均。 */
void zorder_compose(const ZPoly *polys, const unsigned char *colors, int n,
                    const int *order, ZBox win, int w, int h, int ss,
                    unsigned char *out);

/* 渲染入口。enabled=0：旧逻辑，添加顺序二值并集（与 render 模块逐字节一致），
 * 此时 colors/target 忽略可为 NULL；enabled=1：反解顺序并按色合成，
 * 返回值为 zorder_solve 的返回码。 */
int zorder_render_scene(const ZPoly *polys, const unsigned char *colors, int n,
                        const unsigned char *target, ZBox win, int w, int h, int ss,
                        int enabled, unsigned char *out);

/* 遮挡错像素：两图不同字节的个数。 */
int zorder_diff(const unsigned char *a, const unsigned char *b, int w, int h);

#endif
