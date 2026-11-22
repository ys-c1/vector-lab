#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
#include "adapt.h"
typedef struct { double tx, ty, rot, scale; } RPlace;
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

/*
 * adaptive=0：走老的固定 ss=4 路径，与 pipeline_render 逐字节相同。
 * adaptive=1：二分反解达标最小 ss（合法域 1..8），info 可 NULL。
 * 返回 0 成功；负值为参数/场景错误，ADAPT_ERR_NOPASS/NONMONO 透传。
 */
int pipeline_render_opt(const RScene *s, RPlace place, RBox win, int w, int h,
                        int adaptive, double tol, unsigned char *buf,
                        AdaptResult *info);
#endif
