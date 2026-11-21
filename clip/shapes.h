#ifndef CLIP_SHAPES_H
#define CLIP_SHAPES_H
#include "clip.h"

/*
 * U 形凹窗口（CCW，8 顶点）：10x10 方框，顶边开 4x7 的凹口。
 * 手算面积 = 100 - 4*7 = 72；左半(x<=5)手算并集面积 = 36。
 * 凸拆分手算为 3 块：左竖条 30 + 右竖条 21 + 底横条 21 = 72。
 */
static inline int shape_u_window(CPoint *w) {
    CPoint u[8] = {{0,0},{10,0},{10,10},{7,10},{7,3},
                   {3,3},{3,10},{0,10}};
    for (int i = 0; i < 8; ++i) w[i] = u[i];
    return 8;
}

/*
 * 矩形梳齿凹窗口（CCW，64 顶点）：底边 y=0，顶边在 y=10 与 y=5 间交替，
 * 共 15 个 0.6 宽、5 深的凹口，齿与缺口各宽 0.6。
 * 手算面积 = 10*10 - 15*(0.6*5) = 100 - 45 = 55。
 */
static inline int shape_comb_window(CPoint *w) {
    int n = 0;
    w[n++] = (CPoint){0, 0};
    w[n++] = (CPoint){10, 0};
    w[n++] = (CPoint){10, 10};
    for (int i = 0; i < 15; ++i) {
        double notch_x = 9.6 - i * 0.6;
        double tooth_x = notch_x - 0.6;
        w[n++] = (CPoint){notch_x, 10};
        w[n++] = (CPoint){notch_x, 5};
        w[n++] = (CPoint){tooth_x, 5};
        w[n++] = (CPoint){tooth_x, 10};
    }
    w[n++] = (CPoint){0, 10};
    return n;
}

/* 完全罩住窗口的被裁多边形 */
static inline int shape_cover(CPoint *p) {
    CPoint c[4] = {{-20,-20},{30,-20},{30,30},{-20,30}};
    for (int i = 0; i < 4; ++i) p[i] = c[i];
    return 4;
}

/* 只覆盖窗口左半的被裁矩形（U 窗口下手算面积 36） */
static inline int shape_left_half(CPoint *p) {
    CPoint c[4] = {{-20,-20},{5,-20},{5,30},{-20,30}};
    for (int i = 0; i < 4; ++i) p[i] = c[i];
    return 4;
}
#endif
