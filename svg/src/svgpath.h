#ifndef SVGPATH_H
#define SVGPATH_H

typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;

#define SVG_MAX_SUBPATHS 64
#define SVG_MAX_SEGS     256

/* 子路径视图：segs[first .. first+count-1] 是这条子路径的边链，
 * 最后一条边是 Z 生成的闭合边（a=离开点, b=起点）。 */
typedef struct {
    int first;   /* 首段在 VPath.segs 中的下标 */
    int count;   /* 段数（含闭合边） */
    int closed;  /* 是否由 Z/z 显式闭合 */
} VSubPath;

typedef struct {
    VPath path;                      /* 扁平段表，语义与 svg_parse 相同 */
    int sub_count;
    VSubPath subs[SVG_MAX_SUBPATHS];
    int bad_sub;                     /* 出错子路径号（1 起），0 表示无 */
} VSubPaths;

/* 返回码。旧接口 svg_parse 沿用 -1..-5；新接口追加 -6/-7/-8。 */
enum {
    SVG_OK        = 0,
    SVG_E_ARG     = -1,   /* 空指针 / w、h 非正 / 子路径区间越界 */
    SVG_E_NOMOVE  = -2,   /* 首个命令不是 M/m */
    SVG_E_CMD     = -3,   /* 不支持的命令（只支持 M/m/L/l/Z/z） */
    SVG_E_NUM     = -4,   /* 坐标数字缺失 */
    SVG_E_CAP     = -5,   /* 段数 / 子路径数超过上限 */
    SVG_E_EMPTY   = -6,   /* 路径为空（没有任何子路径）；bad_sub = 0 */
    SVG_E_UNCLOSED= -7,   /* 子路径未闭合；bad_sub = 该子路径号（1 起） */
    SVG_E_RULE    = -8    /* 未知填充规则 */
};

/* 填充规则。LEGACY 完全走旧扫描线实现，输出与 svg_fill 逐字节一致。 */
enum {
    SVG_FILL_LEGACY  = 0,
    SVG_FILL_EVENODD = 1,
    SVG_FILL_NONZERO = 2
};

typedef struct {
    int rule;         /* 本次使用的规则 */
    int subpaths;     /* 子路径数 */
    int holes;        /* 洞子路径数 */
    int hole_sub;     /* 第一个洞的子路径号（1 起），无洞为 0 */
    double hole_area; /* 洞面积（坐标单位²），各洞子路径面积之和 */
    int bad_sub;      /* 出错子路径号（1 起），无错为 0 */
    /* 逐子路径明细：子路径号（1 起）、自身围出面积、是否洞。 */
    int sub_is_hole[SVG_MAX_SUBPATHS];
    double sub_area[SVG_MAX_SUBPATHS];
} SVGStats;

/* 旧解析器：行为原样保留（含多子路径时 start 不重置的历史行为）。 */
int svg_parse(const char *d, VPath *out);

/* 按 M/m 切分子路径并记录 Z 闭合；空路径、未闭合子路径报错并给出子路径号。 */
int svg_parse_ex(const char *d, VSubPaths *out);

/* 退化解析：逐式复刻 svg_parse（含历史 start 行为），只额外切出子路径边界，
 * 供 SVG_FILL_LEGACY 使用，保证与旧 svg_fill 输出逐字节一致。 */
int svg_parse_legacy(const char *d, VSubPaths *out);

/* 旧填充：全段奇偶扫描线，输出保持不变。 */
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);

/* 按规则填充。stats 可为 NULL；失败时 stats->bad_sub 给出子路径号。 */
int svg_fill_rule(const VSubPaths *p, int w, int h, unsigned char *buf,
                  int rule, SVGStats *stats);

/* 解析法填充面积（扫描线按顶点 y 分条，条内宽度线性，中点取值精确）。
 * 仅支持 EVENODD / NONZERO；同时填充 stats。 */
int svg_fill_area(const VSubPaths *p, int rule, double *area, SVGStats *stats);

const char *svg_strerror(int code);

int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);

#endif
