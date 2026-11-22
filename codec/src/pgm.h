#ifndef CODEC_PGM_H
#define CODEC_PGM_H
#include <stddef.h>
#include <stdint.h>

/* 读取 PGM（P5 二进制或 P2 文本，支持注释）。
 * 成功返回 0，w、h 填尺寸，调用方 free 输出像素缓冲。 */
int pgm_read(const char *path, uint8_t **px, int *w, int *h);

/* 写标准 P5 PGM（与 render/export.c 的头部完全一致）。 */
int pgm_write_p5(const char *path, const uint8_t *px, int w, int h);
#endif
