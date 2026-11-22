#ifndef RINVSOLVE_H
#define RINVSOLVE_H
#include "scene.h"

/* 反解缩放结果。
 * 像素密度定义：窗口内命中像素数 / 原图形（未缩放）世界面积，单位 像素/面积。 */
typedef struct {
    int degenerate;        /* 是否存在面积为 0 的退化多边形 */
    int degenerate_polys;  /* 退化多边形个数 */
    double area;           /* 场景总面积（鞋带公式，未缩放） */
    double scale_max;      /* 不丢边缘的缩放上限 */
    double scale;          /* 反解得到的最小缩放 */
    long on_pixels;        /* 反解缩放下窗口内命中像素数 */
    double density;        /* 反解缩放下的实际像素密度 */
    long edge_lost;        /* 边缘丢失像素数（成功时必须为 0） */
    int iters;             /* 二分迭代次数 */
    double elapsed_ms;     /* 反解总耗时（含边缘验证），毫秒 */
} InvResult;

/* 边缘验证：在与窗口同格距、同采样相位的扩大网格上栅格化已缩放场景，
 * 返回落在窗口外的命中像素数；cap 为 fullbuf 容量，不足时返回 -1。 */
long inverse_edge_lost(const RScene *scaled, const RBox *win, int w, int h,
                       unsigned char *fullbuf, int cap);

/* 按目标像素密度 target_density 二分最小缩放，并验证边缘不丢。
 * win/w/h：固定输出窗口与分辨率；scale_cap：调用方额外缩放上限，<=0 表示不限制。
 * buf：窗口大小 w*h；fullbuf：边缘验证用扩大缓冲，建议 (w+8)*(h+8)。
 * 返回 0 成功；
 *      -1 参数非法；
 *      -7 退化图形：场景总面积为 0，无法定义密度；
 *      -8 目标密度超出缩放上界（scale_max 字段给出可达上限）。 */
int inverse_solve(const RScene *s, const RBox *win, int w, int h,
                  double target_density, double scale_cap,
                  InvResult *out, unsigned char *buf, unsigned char *fullbuf);
#endif
