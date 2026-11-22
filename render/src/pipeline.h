#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include "cull.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* cfg 为 NULL 或 mode==RCULL_OFF 时与 pipeline_render 逐字节一致。
 * mode==RCULL_AUTO 且 lambda<0 时返回 -5（负代价权重，拒绝，见 cull.h）。 */
int pipeline_render_ex(const RScene *s, RPlace place, RBox win, int w, int h,
                       const RCullCfg *cfg, RCullStats *st, unsigned char *buf);
#endif
