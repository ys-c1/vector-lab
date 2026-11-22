#ifndef WINSPLIT_H
#define WINSPLIT_H
#include <stddef.h>
#include "winnorm.h"

/* 窗口文件装载。
 *
 * 文件格式（纯文本，只含坐标与拆分声明，不依赖被测代码）：
 *
 *   # 注释行跳过
 *   W <窗口号>                       窗口头，号码必须唯一
 *   x y                              顶点行，每个窗口至少 3 个
 *   S <窗口号> <边号 a> <边号 b>     拆分声明：在边 a 与边 b 的交点把窗口切开
 *
 * 边号指输入顶点序列里的边（0 起，边 i 连接顶点 i 与 i+1）。
 * 装载时按声明在交点处把窗口切成若干子环；装载器的拆分口径与
 * winnorm_split_self 独立实现，用来交叉核对后者的结果。
 *
 * 拒绝规则（全部带回窗口号/行号）：
 *   - 顶点行出现在任何窗口头之前、窗口头之前没有顶点
 *   - 窗口号重复
 *   - 每窗口顶点数 < 3 或 > WN_MAX_VERTS
 *   - 拆分声明的窗口号不存在、边号越界、边 a/b 相同或相邻
 *   - 拆分声明的两条边并不真穿过（顶点贴边、共线重叠、不相交）
 *   - 拆分后子环数超过 WN_MAX_RINGS
 */

struct WFileSet_s;

typedef struct {
    int id;                              /* 窗口号 */
    int in_n;                            /* 输入顶点数 */
    WPoint in[WN_MAX_VERTS];
    int nsplit;                          /* 拆分声明条数 */
    int split_a[WN_MAX_CROSS];
    int split_b[WN_MAX_CROSS];
    int rings;                           /* 按声明拆出的子环数 */
    WRing ring[WN_MAX_RINGS];
    double raw_signed[WN_MAX_RINGS];     /* 拆分后、归一化前的有符号面积 */
    double raw_abs[WN_MAX_RINGS];        /* 拆分后、归一化前的面积绝对值 */
} WSWindow;

typedef struct WFileSet_s {
    int count;
    WSWindow win[32];
} WFileSet;

/* 读文件。成功返回 0；失败返回负值，错误说明（含窗口号/行号）写进 err。 */
int wsplit_load(const char *path, WFileSet *out, char *err, size_t errsz);

/* 一个窗口按声明拆分后面积绝对值之和。 */
double wsplit_total_abs(const WSWindow *w);

/* WNorm 结果面积绝对值之和。 */
double wsplit_norm_total_abs(const WNorm *nrm);

#endif
