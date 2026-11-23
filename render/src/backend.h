#ifndef RBACKEND_H
#define RBACKEND_H
#include "scene.h"

/* 可切换光栅后端。选择器取值：
 *   RASTER_BACKEND_SCANLINE(1) 原有逐像素扫描线路径
 *   RASTER_BACKEND_TILE(2)     分块边界跟踪路径
 * 两条后端对同一场景必须逐字节一致。
 */
#define RASTER_BACKEND_SCANLINE 1
#define RASTER_BACKEND_TILE     2
#define RASTER_BACKEND_MIN      RASTER_BACKEND_SCANLINE
#define RASTER_BACKEND_MAX      RASTER_BACKEND_TILE

/* 统一入口。返回 0 成功；负值为错误码：
 *   -1 参数非法（空指针/尺寸非法/ss 越界）
 *   -2 后端选择器为 0 或越界
 *   -5 瓦片后端初始化失败（已清理内部资源）
 */
int raster_render(int backend, const RScene *s, const RBox *box,
                  int w, int h, int ss, unsigned char *buf);

/* 选择器校验：合法返回 0，否则 -1。 */
int raster_backend_valid(int backend);

#endif
