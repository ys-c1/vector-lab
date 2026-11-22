#ifndef RWINROT_H
#define RWINROT_H

#include "scene.h"

/* 反解窗口旋转：窗口是尺寸 win_w x win_h 的矩形，围绕图形包围盒中心旋转
 * theta 角。图形完整覆盖窗口（四角不空）等价于旋转后的窗口仍完全落在图形
 * 的轴对齐包围盒内。求满足这个条件的最大角度。
 *
 * 约束（theta 取 0..90 度，逆时针）：
 *   宽度: win_w * cos(theta) + win_h * sin(theta) <= bw
 *   高度: win_w * sin(theta) + win_h * cos(theta) <= bh
 * 两条曲线在 [0,90] 内先升后降，左侧分支的根即该约束允许的最大角度；
 * 取两条约束的较小者。某条约束在零旋转时就已满足时，它允许到 90 度。
 * 注意：这里求解的是“窗口绕包围盒中心、窗口与包围盒同向”这一族摆放；
 * 窗口可以任意平移摆放时空间是无限的，最大角退化为 90 度。
 */

typedef enum {
    WINROT_OK = 0,
    WINROT_BADARG = -1,      /* 参数非法：非有限、窗口非正、cap 不在 (0,90) */
    WINROT_DEGEN_POINT = -2, /* 包围盒退化成点（宽高都为 0） */
    WINROT_DEGEN_LINE = -3,  /* 包围盒退化成线（宽或高为 0） */
    WINROT_NOFIT = -4,       /* 零旋转时窗口就比包围盒大，空角必然存在 */
    WINROT_OVER_CAP = -5     /* 解出的最大角超过角度上限，按策略拒绝 */
} WinRotStatus;

enum { WINROT_BIND_NONE = 0, WINROT_BIND_WIDTH = 1, WINROT_BIND_HEIGHT = 2 };

typedef struct {
    double bw, bh;            /* 图形轴对齐包围盒 */
    double win_w, win_h;      /* 窗口尺寸 */
    double cap_deg;           /* 角度策略上限，通常 45 */
    double theta_deg;         /* 解出的最大角；OVER_CAP 时为超限的那个解 */
    double theta_width_deg;   /* 宽度约束允许的最大角，无约束为 INFINITY */
    double theta_height_deg;  /* 高度约束允许的最大角，无约束为 INFINITY */
    int binding;              /* 起作用的约束 */
} WinRotSolve;

/* 返回值是 WinRotStatus，0 为成功。结果总是尽量填满 out（含失败时），
 * 便于报出“解出多少度、为什么拒绝”。 */
int winrot_solve(double bw, double bh, double win_w, double win_h,
                 double cap_deg, WinRotSolve *out);

const char *winrot_status_str(int status);
const char *winrot_binding_str(int binding);

/* 把场景按包围盒中心 (cx,cy) 对齐，用旋转了 theta 度的窗口采样：
 * 窗口像素 (x,y) 的中心反变换到场景坐标后做多边形内外测试。窗口物理尺寸
 * 为 win_w x win_h，输出 w x h 像素，ss 为每轴超采样数（1..8）。
 * 输出 255 表示被图形覆盖，0 表示空。 */
int winrot_render(const RScene *s, double cx, double cy, double win_w,
                  double win_h, double theta_deg, int w, int h, int ss,
                  unsigned char *buf);

/* 四角方块边长：max(1, min(w,h)/8)，且不超过短边的一半。 */
int winrot_corner_box(int w, int h);

/* 四个角方块内值为 0 的像素总数。 */
int winrot_count_corner_empty(const unsigned char *buf, int w, int h);

/* 整幅图中值为 0 的像素总数。 */
int winrot_count_empty(const unsigned char *buf, int w, int h);

#endif
