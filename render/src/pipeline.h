#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

typedef struct {
    double angle; /* 反解出的旋转角, 归一化到 (-pi, pi] */
    double dev;   /* 落点偏差, 单位: 像素 */
    long turns;   /* 归一化抹掉的整圈数, 非 0 说明角度跑了多圈 */
    double usec;  /* 该角点求解耗时, 微秒 */
} RCorner;

/* 由目标像素反解旋转角: 对场景包围盒四个角点
 * (minx,miny) (maxx,miny) (maxx,maxy) (minx,maxy),
 * 各给目标像素 target_px[k](整数像素坐标, 对准像素中心),
 * 闭式反推 place.rot 后再做一次牛顿迭代修正。
 * 返回 0 成功(四角点偏差均 <= 1 像素);
 * -1 参数为空/尺寸非法/场景为空; -2 place.scale 为 0;
 * -3 win 窗口为空; -4 某角点落点偏差超过 1 像素。 */
int pipeline_solve_rot(const RScene *s, RPlace place, RBox win, int w, int h,
                       const RPoint target_px[4], RCorner out[4]);
#endif
