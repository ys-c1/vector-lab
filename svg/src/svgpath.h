#ifndef SVGPATH_H
#define SVGPATH_H
typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;
int svg_parse(const char *d, VPath *out);
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);
#endif
