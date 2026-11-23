#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include "fpconf.h"
typedef struct { double tx, ty, rot, scale; } RPlace;

/* 基线入口：保持原有求值表达式与顺序，回落行为逐字节不变。 */
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

/* 确定性入口：按 fp_deterministic 选择路径。
 * on  -> 禁用乘加合并、固定求值顺序；
 * off -> 与 pipeline_render 完全相同的基线路径。
 */
int pipeline_render_cfg(const RScene *s, RPlace place, RBox win, int w, int h,
                        const RFPConf *cfg, unsigned char *buf);
#endif
