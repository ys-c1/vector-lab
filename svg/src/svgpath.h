#ifndef SVGPATH_H
#define SVGPATH_H

/* 相对/绝对混用开关：置 0 后 svg_parse 退化为最初仅 M/L/Z 的版本，
 * 行为（含错误码）与初版逐字节一致。 */
#ifndef SVG_REL
#define SVG_REL 1
#endif

typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;
int svg_parse(const char *d, VPath *out);

#if SVG_REL
/* 支持 M/m/L/l/H/h/V/v/Z/z 混用；相对量按当前笔位累加。
 * 返回 0 成功；-1 参数空；-2 起始不是 M/m；-3 非法指令；
 * -4 坐标缺失（含相对量缺失）；-5 段数超限。
 * err_seg 非 NULL 时，失败则写入出错段号（从 1 计，按指令出现次序）。 */
int svg_parse_ex(const char *d, VPath *out, int *err_seg);
#endif

int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);
#endif
