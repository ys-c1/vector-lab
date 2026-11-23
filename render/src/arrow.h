#ifndef RARROW_H
#define RARROW_H
#include "scene.h"
#include "pipeline.h"

#define ARROW_MAX 64

/* 错误码：密度为零与路径长度为零各有明确码 */
enum {
    ARROW_OK = 0,
    ARROW_EARGS = -1,    /* 空指针或点数非法 */
    ARROW_EDENSITY = -2, /* 密度 <= 0 */
    ARROW_ELENGTH = -3,  /* 路径总长度为 0 */
    ARROW_EFULL = -4     /* 箭头数超 ARROW_MAX */
};

typedef struct { double x, y, angle; } RArrow;
typedef struct { int n; RArrow a[ARROW_MAX]; } RArrowSet;

/* 按弧长等距在折线路径上布置箭头，朝向取所在线段切线。
 * density 为每单位弧长的箭头数；居中排布（首尾各留半个间距）。 */
int arrow_layout(const RPoint *pts, int n, double density, RArrowSet *out);

/* 渲染场景并沿路径叠加方向箭头三角形（arrow_len 为世界坐标长度）。
 * 箭头关闭（path 为 NULL 或 npath < 2）时退化为 pipeline_render，
 * 输出与当前构建逐字节一致。 */
int pipeline_render_arrows(const RScene *s, RPlace place, RBox win, int w, int h,
                           const RPoint *path, int npath, double density,
                           double arrow_len, unsigned char *buf);
#endif
