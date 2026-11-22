#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* mode 0：旧逻辑（与 pipeline_render 逐字节一致，新逻辑关闭）；
 * mode 1：覆盖率采样，采样相位 (phx, phy)，0.5 为居中。 */
int pipeline_render2(const RScene *s, RPlace place, RBox win, int w, int h,
                     int mode, double phx, double phy, unsigned char *buf);
#endif
