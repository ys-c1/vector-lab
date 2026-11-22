#ifndef SVGPATH_H
#define SVGPATH_H
/* 平滑段（S/T）开关：置 0 时 svg_parse2 退化为 svg_parse，输出与旧版一致 */
#ifndef SVG_SMOOTH
#define SVG_SMOOTH 1
#endif
typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;
/* 接缝记录：at[i] 是第 i 个平滑段（S/T）细分后首条线段在 VPath.segs 中的
   下标，segs[at[i]-1] 即前一段曲线的末条线段。 */
typedef struct { int count; int at[64]; } VSeams;
int svg_parse(const char *d, VPath *out);
/* 平滑段扩展：在 svg_parse 基础上接受 C/c S/s Q/q T/t。曲线按 subdiv 段
   均匀细分（钳到 1..64）成直线段后填图，与 svg_fill 走同一路径。
   S/T 的前一绘制段不是匹配曲线（S 需三次、T 需二次）时返回 -6，err_seg
   非空时存入出错段号（按 L/Z/C/S/Q/T 绘制命令从 0 计）。seams 可空。
   返回码沿用 svg_parse，另加 -6。编译期 -DSVG_SMOOTH=0 时退化为
   svg_parse，输出与旧版逐字节相同。 */
int svg_parse2(const char *d, VPath *out, int subdiv, int *err_seg, VSeams *seams);
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);
#endif
