#ifndef WINSOLVE_H
#define WINSOLVE_H
#include <stddef.h>
#include "winnorm.h"
#include "winsplit.h"

/* 一组窗口的批量处理：检测 -> 比对文件里声明的拆分 -> 逐窗口归一化 ->
 * 统一绕序 -> 可选出图。所有窗口共用一块画布，画布包围盒 = 全部输入顶点
 * 的并集（与 render 模块 scene_bbox 的口径一致）。 */

#define WS_MAX_WINDOWS 16

typedef struct {
    int id;                          /* 窗口号 */
    int in_n;                        /* 输入顶点数 */
    WPoint in[WN_MAX_VERTS];
    int nsplit;                      /* 文件声明的拆分条数 */
    int split_a[WN_MAX_CROSS];
    int split_b[WN_MAX_CROSS];
    int declared_rings;              /* 按声明拆出的子环数（0 = 未声明） */
    int rings;                       /* 模块复算的子环数 */
    int split_ok;                    /* 声明拆分与模块复算是否一致 */
    WNorm nrm;                       /* 归一化结果 */
    int rc;                          /* 归一化返回码 */
    int at, at2;                     /* 归一化失败的涉事序号 */
    double total_abs;                /* 归一化后面积绝对值之和 */
} WItem;

typedef struct {
    int count;
    WItem win[WS_MAX_WINDOWS];
} WSet;

typedef struct { double minx, miny, maxx, maxy; } WBox;

/* 由装载结果构造批量输入并逐窗口归一化。
 * declared_rings 来自文件声明的拆分；split_ok 表示两者子环面积多重集一致。 */
void ws_build(const WFileSet *src, WSet *dst);

/* 画布包围盒：全部窗口输入顶点的并集。空集合返回 -1。 */
int ws_bbox(const WSet *s, WBox *out);

/* 出图：mode=0 用输入顶点原样（奇偶规则，老路径，逐字节复刻
 * render/src/raster.c）；mode=1 用归一化后的全部子环（把各子环的边并成
 * 一个边集再数奇偶，新路径）。
 *
 * 为什么不换成非零环绕：把每个子环都统一成逆时针之后，非零环绕会把
 * "被覆盖两次的区域"（星形/五角星的中心、嵌套且原方向相反的子环）
 * 填实，而输入用奇偶规则时那里是洞——那才是真正的几何改变。奇偶覆盖
 * 与子环方向无关，拆环+逆序都不改变边集，因此新路径与老路径必然逐
 * 像素相同，几何严格不变。统一后的绕序作为数据交给下游使用。 */
int ws_render(const WSet *s, WBox win, int w, int h, int ss, int mode,
              unsigned char *buf);

/* 两图逐字节不同的像素数。 */
long ws_diff(const unsigned char *a, const unsigned char *b, int w, int h);

/* PGM 导出（P5）。 */
int ws_save_pgm(const char *path, const unsigned char *buf, int w, int h);

#endif
