#ifndef RASPECT_H
#define RASPECT_H
#include "scene.h"

/* 窗口四条边按方向分组，用于零/负比值时指出是哪条边 */
#define ASPECT_EDGE_NONE 0
#define ASPECT_EDGE_LR   1  /* 左边/右边（宽度 Ww 方向） */
#define ASPECT_EDGE_BT   2  /* 下边/上边（高度 Wh 方向） */
#define ASPECT_EDGE_BOTH 3  /* 四条边全部重合（退化成点） */

/* 退化形状分类 */
#define ASPECT_DEGEN_NONE  0
#define ASPECT_DEGEN_POINT 1  /* 宽高均为零：退化成点 */
#define ASPECT_DEGEN_LINE  2  /* 宽或高其一为零：退化成线 */

typedef struct {
    RBox win;              /* 反解出的窗口四边 */
    double ratio;          /* 解出的窗口纵横比 Ww / Wh */
    double px_ratio;       /* 像素纵横比 w / h */
    double kx, ky;         /* 窗口相对场景包围盒的扩张倍数 */
    int bad_edge;          /* 比值为零或负时，出问题的那组边 */
    int degenerate;        /* 退化标记（点 / 线） */
} ASolve;

/* 反解窗口纵横比。
 * 方程：像素映射两轴等比例  <=>  Ww / Wh = w / h。
 * 约束：场景包围盒居中放入窗口，且至少一组边与窗口贴合，得到唯一四边。
 * 返回 0 成功；<0 拒绝（原因见 out->bad_edge / out->degenerate）：
 *   -2 像素宽或高非正（bad_edge 指出方向）
 *   -3 包围盒比值为零或负（退化点/线，bad_edge 指出重合的边） */
int aspect_solve(const RBox *bbox, int w, int h, ASolve *out);

/* 由二值缓冲测圆度：rx/ry 为亮像素 x/y 坐标相对质心的标准差，
 * 圆度偏差 dev = |rx - ry| / min(rx, ry)。 */
int aspect_roundness(const unsigned char *buf, int w, int h,
                     double *rx, double *ry, double *dev);

const char *aspect_edge_name(int edge);
const char *aspect_degen_name(int d);
#endif
