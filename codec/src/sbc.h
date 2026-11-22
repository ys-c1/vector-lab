#ifndef CODEC_SBC_H
#define CODEC_SBC_H
#include <stddef.h>
#include <stdint.h>
#include "quant.h"

#define SBC_MAGIC0 'S'
#define SBC_MAGIC1 'B'
#define SBC_MAGIC2 'C'
#define SBC_MAGIC3 '1'
#define SBC_HEADER_SIZE 16
#define SBC_MAX_DIM 65535

typedef struct {
    uint16_t w, h;
    uint8_t levels, quality;
    int lossless;                /* 所有子带常数直通 => 整帧无损 */
} SbcInfo;

/* 编码：8 位灰度像素 -> .sbc 字节流。返回字节数，<0 为错误。
 * 输出缓冲由调用方提供，容量至少 sbc_max_size(w,h)。 */
size_t sbc_max_size(int w, int h);
int sbc_encode(const uint8_t *px, int w, int h, QPreset q, uint8_t *out, size_t cap);

/* 解码并校验：.sbc 字节流 -> 8 位灰度像素。
 * 任一字段非法（魔数、尺寸、级数、零/负步长等）均拒绝，返回负错误码。 */
int sbc_decode(const uint8_t *data, size_t len, uint8_t *px, size_t pxcap, SbcInfo *info);

/* 仅读头部信息（不解码）。 */
int sbc_probe(const uint8_t *data, size_t len, SbcInfo *info);

/* PSNR（dB）；完全一致时返回 99.0。 */
double sbc_psnr(const uint8_t *a, const uint8_t *b, size_t n);
#endif
