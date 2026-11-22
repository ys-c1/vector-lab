#ifndef SVGPATH_H
#define SVGPATH_H
typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;
int svg_parse(const char *d, VPath *out);
/* 光栅化：每像素 8x8 子点奇偶环绕求覆盖率，坐标按路径包围盒映射到 w*h。
 * 返回 0 成功；-1 参数非法（空指针或 w/h 非正）；-2 路径为空没有段。
 * 光栅工作集仅调用方提供的 w*h 字节缓冲，不额外分配图像级内存。 */
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
/* 写二进制 PGM（P5）。返回 0 成功；-1 参数非法；-2 无法打开文件。 */
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);
#endif
