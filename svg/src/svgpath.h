#ifndef SVGPATH_H
#define SVGPATH_H

typedef struct { double x, y; } VPoint;
typedef enum { SV_MOVE = 1, SV_LINE = 2 } VSegKind;
typedef struct { VSegKind kind; VPoint a, b; } VSeg;
typedef struct { int count; VSeg segs[256]; } VPath;

int svg_parse(const char *d, VPath *out);
int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);

/*
 * viewBox -> 目标像素的单位换算。
 *
 * 现状 svg_fill 直接拿路径坐标当像素坐标用，viewBox 与导出尺寸不一致时
 * 整张图会按比例缩错。本组接口把两套坐标分开：
 *
 *   sx = target_w / viewBox.w        X = x * sx + tx,  tx = -viewBox.minx * sx
 *   sy = target_h / viewBox.h        Y = y * sy + ty,  ty = -viewBox.miny * sy
 *
 * 光栅化时再对采样点做像素中心校正：第 (px,py) 个像素的判定点是设备坐标
 * (px+0.5, py+0.5)，反算回用户坐标
 *
 *   ux = (px + 0.5 - tx) / sx = viewBox.minx + (px + 0.5) / sx
 *   uy = (py + 0.5 - ty) / sy = viewBox.miny + (py + 0.5) / sy
 *
 * 于是边界判定最多差半个像素：轴向 0.5 px，二维欧氏上界
 * edge_px = 0.5*sqrt(2) ≈ 0.7071 px；换算回用户单位的上界存在 edge_user。
 *
 * 错误字段名（err_field，容量 SV_FIELD_MAX）：
 *   "viewBox" / "viewBox.minx" / "viewBox.miny" / "viewBox.w" / "viewBox.h"
 *   "target.w" / "target.h" / "flags" / "path" / "buf" / "out"
 *
 * 关闭 SV_F_VIEWBOX 时 svg_fill_mapped 直接走旧的 svg_fill 路径，
 * 产物与现状逐像素相同（viewBox 参数被忽略）。
 */

#define SV_FIELD_MAX 32

enum {
    SV_OK = 0,
    SV_E_ARG = -1,      /* 空指针，field = "viewBox"/"out"/"path"/"buf" */
    SV_E_VIEWBOX = -2,  /* viewBox 缺项/非有限/宽高非正，field 指向具体字段 */
    SV_E_TARGET = -3,   /* 目标宽或高非正，field = "target.w"/"target.h" */
    SV_E_FLAGS = -4     /* 未知标志位，field = "flags" */
};

#define SV_F_VIEWBOX 1u  /* 打开 viewBox 单位换算 */

typedef struct { double minx, miny, w, h; } VBox;

typedef struct {
    double sx, sy;      /* 每用户单位对应的像素数（可不等比） */
    double tx, ty;      /* 设备偏移：X = x*sx + tx, Y = y*sy + ty */
    double aspect_err;  /* 宽高比误差 |sx/sy - 1|（即目标图与 viewBox 的形变） */
    double edge_px;     /* 采样点像素中心校正的欧氏上界 0.5*sqrt(2) px */
    double edge_user;   /* 同一上界换算回用户单位 */
} VMap;

/* 解析 "minx miny w h"（空白/逗号分隔）。仅检查数量与有限性，宽高是否
 * 为零留给 svg_map_fit 报字段。成功返回 SV_OK，否则 SV_E_VIEWBOX。 */
int svg_parse_viewbox(const char *s, VBox *out, char err_field[SV_FIELD_MAX]);

/* 由 viewBox 与目标尺寸计算 sx/sy/tx/ty 及误差指标。
 * viewBox.w/h 为零或负 -> SV_E_VIEWBOX（field "viewBox.w"/"viewBox.h"）；
 * 目标宽/高为零或负 -> SV_E_TARGET（field "target.w"/"target.h"）。 */
int svg_map_fit(const VBox *vb, int w, int h, VMap *out,
                char err_field[SV_FIELD_MAX]);

/* 用户坐标 -> 设备像素坐标。 */
VPoint svg_map_apply(const VMap *m, VPoint p);

/* 像素 (px,py) 的中心反算回用户坐标（像素中心校正）。 */
VPoint svg_map_sample(const VMap *m, int px, int py);

/* 带单位换算的光栅化。flags 含 SV_F_VIEWBOX 时按 vb 映射并做像素中心
 * 校正；不含时等价于 svg_fill(p,w,h,buf)，逐像素相同。 */
int svg_fill_mapped(const VPath *p, const VBox *vb, int w, int h,
                    unsigned flags, unsigned char *buf,
                    char err_field[SV_FIELD_MAX]);

#endif
