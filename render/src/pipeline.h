#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

/*
 * 自适应旋转渲染（opt_rot=1 启用，opt_rot=0 时与 pipeline_render 行为一致）。
 * 启用后先用 fit 模块求最小面积包围盒角，把场景旋转 -angle 再渲染，
 * 并用旋转后的包围盒作为窗口。*angle 回传实际使用的角度（弧度）。
 * 角度合法域为 [0, pi/2)，fit 内部保证；外部传入非法角时返回 -5。
 */
int pipeline_render_fit(const RScene *s, RPlace place, int w, int h, int opt_rot,
                        unsigned char *buf, double *angle);
#endif
