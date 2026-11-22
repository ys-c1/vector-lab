#ifndef REXPORT_H
#define REXPORT_H
#include <stddef.h>

/* P5 头部 "P5\n%d %d\n255\n" 的实际字节数（含末尾换行）。 */
int export_pgm_header_len(int w, int h);
/* 给定尺寸导出的 PGM 总字节数；尺寸非法时返回负值。 */
long long export_pgm_bytes(int w, int h);
int export_pgm(const char *path, const unsigned char *buf, int w, int h);
int export_count_on(const unsigned char *buf, int w, int h);
#endif
