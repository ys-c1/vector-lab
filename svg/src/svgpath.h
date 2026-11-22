#ifndef SVGPATH_H
#define SVGPATH_H
typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;
int svg_parse(const char *d, VPath *out);
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);

#ifdef SVGPATH_MULTILAYER
/* 新逻辑：多层轮廓入口（默认关闭；关闭时旧 fill 路径逐字节不变）。
 * 把 VPath 按 M 断点切成子轮廓，奇偶缠绕规则下逐层填：
 * 第 0 层填充，奇数层挖空、偶数层再填回，使内层轮廓可被独立核验。*/
int svg_fill_layers(const VPath *p, int w, int h, unsigned char *buf);
#endif
#endif
