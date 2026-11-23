#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include "tilecache.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);
/* tc 为 NULL（缓存关闭）时走原始代码路径，输出与 pipeline_render 逐字节相同。 */
int pipeline_render_tc(const RScene *s, RPlace place, RBox win, int w, int h,
                       unsigned char *buf, TileCache *tc);
#endif
