#ifndef RASTERTILE_H
#define RASTERTILE_H
#include "raster.h"

/* 分块边界跟踪后端。
 * 图像切成固定瓦片，逐瓦片：包围盒保守剔除无关多边形，
 * 块内沿采样行走边求跨、按奇偶规则填像素。
 * 与 raster_fill（扫描线后端）对同一场景逐字节一致。
 *
 * 返回 0 成功，-1 参数非法，-5 初始化失败（内部资源已清理）。
 */
int raster_fill_tiled(const RScene *s, const RBox *box, int w, int h,
                      int ss, unsigned char *buf);

#endif
