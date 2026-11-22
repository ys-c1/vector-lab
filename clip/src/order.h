#ifndef CLIP_ORDER_H
#define CLIP_ORDER_H

#include "clip.h"

/* 窗口校验结果 */
enum {
    CLIP_ORD_OK = 0,         /* 窗口有效 */
    CLIP_ORD_EMPTY = 1,      /* 窗口为空：边数不足 3 或面积为零 */
    CLIP_ORD_COINCIDENT = 2, /* 边重合：存在零长边或共线边 */
    CLIP_ORD_TOOMANY = 3     /* 边数超过枚举上限 */
};

/* 反解枚举支持的窗口边数上限（8! = 40320 种处理顺序） */
#define CLIP_ORD_MAX_EDGES 8

const char *clip_order_err(int code);

/* 校验窗口：空窗口或含重合边时返回对应错误码，否则 CLIP_ORD_OK */
int clip_check_window(const CPoint *win, int wn);

/* 按指定边序裁剪；order 为 NULL 时按自然顺序，结果与 clip_convex 一致。
 * stages 非空时写入每一阶段处理后的顶点数（长度 wn）。 */
int clip_convex_ordered(const CPoint *poly, int n, const CPoint *win, int wn,
                        const int *order, CPoly *out, int *stages);

/* 反解最佳边序：枚举全部 wn! 种窗口边处理顺序，取输出顶点数最少者；
 * 顶点数相同按边号字典序取最小（先枚举到的胜出）。
 * best_order 非空时输出最优边序（长度 wn）。
 * 返回 0 成功；窗口非法返回对应校验错误码（正数）；参数非法返回 -1。 */
int clip_best_order(const CPoint *poly, int n, const CPoint *win, int wn,
                    int *best_order, CPoly *out);

#endif
