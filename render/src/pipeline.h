#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
/* 旧路径：固定 ss=4 的二值填充，保持历史逐字节行为。 */
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* 新路径：显式给定 ss，覆盖率灰度填充。 */
int pipeline_render_ss(const RScene *s, RPlace place, RBox win, int w, int h, int ss,
                       unsigned char *buf);
/* 把场景按 place 变换后写入 *moved；窗口取 moved 的 bbox（若 out_win 非空）。 */
int pipeline_place(const RScene *s, RPlace place, RScene *moved, RBox *out_win);
#endif
