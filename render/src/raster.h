#ifndef RRASTER_H
#define RRASTER_H
#include "scene.h"
int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf);
/* 只填 (x0,y0) 起 rw x rh 的像素区；采样坐标按整幅 w x h 的同一相位计算，
 * 因此窗口内的每个字节与 raster_fill 整幅渲染的对应字节完全相同。
 * 窗口必须完全落在图内，否则返回 -1。 */
int raster_fill_region(const RScene *s, const RBox *box, int w, int h, int ss,
                       int x0, int y0, int rw, int rh, unsigned char *buf);
#endif
