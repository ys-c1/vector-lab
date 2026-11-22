#ifndef RMARGIN_H
#define RMARGIN_H
#include "scene.h"

/* 四边留边，单位：像素（按求解时窗口的像素精度换算） */
typedef struct { int left, right, bottom, top; } RMargin;

#define RMARGIN_OK     0  /* 解出留边 */
#define RMARGIN_EMPTY  1  /* 空场景：无包围盒可解（由 pipeline 层判定） */
#define RMARGIN_REJECT 2  /* 留边超过画面一成，拒绝 */
#define RMARGIN_BADARG -1

/* 反解留边：按当前包围盒 bb、窗口 win 与像素精度列不等式，解每边至少留多少像素。
 * 拒绝（RMARGIN_REJECT）时 out 同样写入解出的值，便于上报。 */
int margin_solve(const RBox *bb, const RBox *win, int w, int h, RMargin *out);

/* 按留边扩窗；留边像素按原窗口的像素精度换算成世界单位。 */
RBox margin_expand(const RBox *win, int w, int h, RMargin m);

#endif
