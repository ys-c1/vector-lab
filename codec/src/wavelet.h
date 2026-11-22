#ifndef CODEC_WAVELET_H
#define CODEC_WAVELET_H
#include <stdint.h>

/* 子带视图：系数缓冲区中的一块矩形，stride 恒为整图宽度。 */
typedef struct { int x, y, w, h; } WBand;

/* 根据边长自动选分解级数（1..6）。 */
int wavelet_levels(int w, int h);

/* 多级二维整数 Haar（S 提升）正变换，原地进行。
 * bands 至少容纳 3*levels+1 项：
 *   bands[0] 为最粗 LL，其后按级（粗->细）依次 HL, LH, HH。 */
int wavelet_forward(int32_t *a, int w, int h, int levels, WBand *bands);

/* 对应逆变换，重建后系数写回原缓冲区。 */
int wavelet_inverse(int32_t *a, int w, int h, int levels);

/* 只计算子带几何布局（解码侧使用），与 forward 写入的 bands 一致。 */
int wavelet_layout(int w, int h, int levels, WBand *bands);
#endif
