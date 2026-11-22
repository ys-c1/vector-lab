#ifndef CLIP_GROUPS_H
#define CLIP_GROUPS_H

#include "clip.h"

/* 演示与测试共用的四组窗口：锯齿带多边形 + 矩形窗口。
 * 默认边序下中间阶段顶点数暴涨、顶到 62 槽缓冲上限；
 * 反解出的最优边序先裁掉大片区域，最终顶点数少三成以上。 */

typedef struct {
    int teeth;                                   /* 每排牙数 */
    double x0, pitch;                            /* 起始 x 与牙距 */
    double ybase, ytipd, ytop, ytipu;            /* 下排基线/牙尖、上排基线/牙尖 */
    double wx1;                                  /* 窗口 [0,wx1] x [0,10] */
} ClipGroup;

static const ClipGroup CLIP_GROUPS[4] = {
    { 13,  -6.0, 3.0, 2.0, -3.0, 6.0, 11.0,  8.0 },
    { 13,  -6.0, 3.5, 3.0, -2.0, 7.0, 12.0, 10.0 },
    { 13,  -6.0, 4.0, 2.0, -3.0, 6.0, 11.0, 12.0 },
    { 14, -12.0, 3.0, 2.0, -3.0, 6.0, 11.0,  8.0 },
};

static inline int band_poly(CPoint *p, int teeth, double x0, double pitch,
                            double ybase, double ytipd, double ytop, double ytipu) {
    int n = 0;
    for (int i = 0; i < teeth; ++i) {
        p[n].x = x0 + i * pitch;           p[n].y = ybase; ++n;
        p[n].x = x0 + i * pitch + pitch / 2; p[n].y = ytipd; ++n;
    }
    for (int i = teeth - 1; i >= 0; --i) {
        p[n].x = x0 + i * pitch + pitch / 2; p[n].y = ytop; ++n;
        p[n].x = x0 + i * pitch;           p[n].y = ytipu; ++n;
    }
    return n;
}

static inline int group_poly(int g, CPoint *p) {
    const ClipGroup *cg = &CLIP_GROUPS[g];
    return band_poly(p, cg->teeth, cg->x0, cg->pitch,
                     cg->ybase, cg->ytipd, cg->ytop, cg->ytipu);
}

static inline void group_win(int g, CPoint *w) {
    double x1 = CLIP_GROUPS[g].wx1;
    w[0].x = 0.0; w[0].y = 0.0;
    w[1].x = x1;  w[1].y = 0.0;
    w[2].x = x1;  w[2].y = 10.0;
    w[3].x = 0.0; w[3].y = 10.0;
}

#endif
