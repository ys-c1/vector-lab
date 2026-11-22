#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"

typedef struct { double tx, ty, rot, scale; } RPlace;

/* 伽马导出选项。enabled=0 时 pipeline_render_gamma 与 pipeline_render
 * 走同一条代码，输出逐字节相同。 */
typedef struct { int enabled; double gamma; } RExportGamma;

int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

/* 线性光栅化后按 gamma 编码：y = round(255*(x/255)^(1/gamma))。
 * 失败返回：-1..-4 同 pipeline_render；-10 gamma 非法（<=0/非有限）。 */
int pipeline_render_gamma(const RScene *s, RPlace place, RBox win, int w, int h,
                          unsigned char *buf, const RExportGamma *opt);
#endif
