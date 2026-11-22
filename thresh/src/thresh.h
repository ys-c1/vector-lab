#ifndef THRESH_H
#define THRESH_H

#include <stdio.h>

/* 8 位灰度图（对应 PGM P5/maxval=255） */
typedef struct {
    int w, h;
    unsigned char *pix;
} TImage;

typedef struct {
    long hist[256];   /* hist[g] = 灰度值恰为 g 的像素数 */
    long total;
} THist;

/* 反解结果状态 */
typedef enum {
    T_OK = 0,        /* 阈值在 [0,255] 内反解成功 */
    T_EMPTY = -1,    /* 空图 */
    T_BLACK = -2,    /* 纯黑：任何阈值下亮像素占比恒为 0 */
    T_WHITE = -3,    /* 纯白：任何阈值下亮像素占比恒为 1 */
    T_UNREACH = -4   /* 目标占比落在直方图台阶的跳变空隙中，无阈值可达 */
} TStatus;

/* 读 P5 格式 PGM；只接受 maxval=255。成功返回 0，失败返回负数。 */
int thresh_load_pgm(const char *path, TImage *out);
void thresh_free(TImage *img);

/* 写 P5 格式 PGM。成功返回 0，失败返回负数。 */
int thresh_save_pgm(const char *path, const TImage *img);

/* 构建灰度直方图。返回 0 或负数错误码。 */
int thresh_hist(const TImage *img, THist *h);

/* 亮像素占比关于阈值 t 的单调函数。
 * 亮定义为 gray >= t。t 必须落在 [0,256]：
 *   t=0   -> 恒为 1（全部像素都亮）
 *   t=256 -> 恒为 0（没有像素能达到 256）
 * F 在 [0,256] 上关于 t 单调不增。 */
double thresh_ratio(const THist *h, int t);

/* 反解阈值：找 t in [0,255] 使 F(t) 落在 [lo,hi]（含端点）。
 * *out_t 为反解阈值，*out_ratio 为该阈值下的实际亮像素占比。
 * 纯黑/纯白/不可达按 TStatus 返回负数；lo/hi 非法（越界或 lo>hi）返回 T_EMPTY。 */
TStatus thresh_invert(const THist *h, double lo, double hi, int *out_t, double *out_ratio);

/* 反解失败诊断：给出目标区间两侧最近的可达占比台阶。
 * r_below 收到小于 lo 的最大可达占比（不存在则为 -1.0）；
 * r_above 收到大于 hi 的最小可达占比（不存在则为 -1.0）。
 * 两者之间的空隙就是任何整数阈值都跨不过的跳变。 */
void thresh_flanking_steps(const THist *h, double lo, double hi,
                           double *r_below, double *r_above);

/* 按固定阈值二值化：gray >= t -> 255，否则 0。t 必须在 [0,255]。 */
int thresh_apply_fixed(const TImage *img, int t, TImage *out);

/* 判定纯黑（全 0）/纯白（全 255）。 */
int thresh_is_black(const THist *h);
int thresh_is_white(const THist *h);

#endif
