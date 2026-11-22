#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include "margin.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* 带反解留边的重绘：按 place 变换后求包围盒，按当前像素精度解出每边留边，
 * 扩窗再渲染，保证场景不被切。返回 0 成功；RMARGIN_EMPTY 空场景；
 * RMARGIN_REJECT 留边超画面一成被拒绝；负值含义同 pipeline_render。
 * used 非空时回传解出的留边（被拒绝时也回传）。 */
int pipeline_render_fit(const RScene *s, RPlace place, RBox win, int w, int h,
                        unsigned char *buf, RMargin *used);
#endif
