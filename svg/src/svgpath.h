#ifndef SVGPATH_H
#define SVGPATH_H

#include <math.h>   /* NAN */

typedef struct { double x, y; } VPoint;

typedef enum {
    SV_MOVE = 1,    /* 仅作起点；旧填充把它当直线段处理，行为不变 */
    SV_LINE = 2,
    SV_ARC  = 3     /* 椭圆弧，参数见 VSeg 的 rx/ry/xrot/large/sweep */
} VSegKind;

#define SVG_PATH_MAX 256   /* 段容量；与旧实现一致，最多写入 255 段 */

typedef struct {
    VSegKind kind;
    VPoint a, b;        /* LINE：起止点；ARC：弧起点 a、弧终点 b */
    /* 以下字段仅 SV_ARC 使用。旧式聚合初始化（只写前三个成员）仍可编译，
     * 其余成员为 0，但 GCC 会报 -Wmissing-field-initializers 警告；
     * 介意的调用方可 memset 后逐个赋值，或关闭该警告。 */
    double rx, ry;      /* 输入半径，非零（负值按 F.6.2 取绝对值；不含放大） */
    double xrot;        /* x 轴旋转角，弧度，|xrot| <= π/2 */
    int large, sweep;   /* 大弧标志 / 扫描方向，仅 0 或 1 */
} VSeg;

typedef struct { int count; VSeg segs[SVG_PATH_MAX]; } VPath;

#define SVG_DEFAULT_TOL 1e-3
#define SVG_ARC_CHORD_CAP 8192

/* ------------------------------------------------------------------ *
 * 解析
 *
 * svg_parse 保持旧行为：只认 M/m L/l Z/z，遇到 A/a 返回 SVG_E_CMD。
 * svg_parse_flags 加 SVG_F_ARCS 后才解析 A/a：
 *     A rx ry xrot large-arc-flag sweep-flag x y
 * 相对命令 a 的 x/y 相对当前点，rx/ry/xrot 不相对；xrot 单位为度，
 * 合法区间按字面取 [-90, 90]（不做 360° 折回：450° 也按越界报错）。
 * 每个命令字母只吃一组参数（与旧解析器一致，不做隐式重复）。
 *
 * 弧段在解析期只检查“是不是数字、是不是有限值”，几何合法性留到
 * svg_arc_frame / 填充时检查：兼容整圆模式不看 ry/xrot/large/sweep，
 * 不能因为打开 A/a 解析就拒绝一条现状能画出来的整圆。
 * 解析期错误经 svg_last_error_detail() 给出：
 *   seg    0 基段号，按输出路径的段序计，M 不占号
 *   field  出错字段名，如 "rx"/"ry"/"xrot"/"x"/"y"/"large"/"sweep"
 *   value  出错数值（无对应数值时为 NAN）
 *
 *   任一参数非有限                    -> SVG_E_NONFINITE   field=参数名
 *   large/sweep 不是 0 或 1           -> SVG_E_NUM        field="large"/"sweep"
 *
 * 几何检查（svg_arc_frame 与椭圆弧填充）：
 *   rx==0 或 ry==0（负半径按 F.6.2 取绝对值） -> SVG_E_RADIUS field="rx"/"ry"
 *   半径不足以连接起止点（Λ>1）      -> SVG_E_RADIUS      field="rx/ry"
 *   |xrot| > 90°                     -> SVG_E_XROT        field="xrot"
 *   起止点重合                       -> SVG_E_COINCIDENT  field="x/y"
 * ------------------------------------------------------------------ */
int svg_parse(const char *d, VPath *out);
int svg_parse_flags(const char *d, VPath *out, unsigned flags);

#define SVG_F_ARCS 1u   /* 打开 A/a 椭圆弧解析 */

enum {
    SVG_OK           =   0,
    SVG_E_ARG        =  -1,   /* 空指针 / 非法参数 */
    SVG_E_START      =  -2,   /* 路径不以 M/m 开头 */
    SVG_E_CMD        =  -3,   /* 不支持的命令 */
    SVG_E_NUM        =  -4,   /* 数值缺失或不是数字 */
    SVG_E_CAP        =  -5,   /* 段数 / 弦数超出容量 */
    SVG_E_RADIUS     =  -6,   /* 椭圆弧半径非正或不足以连接起止点 */
    SVG_E_COINCIDENT =  -7,   /* 椭圆弧起止点重合 */
    SVG_E_XROT       =  -8,   /* 旋转角越界（|xrot| > 90°） */
    SVG_E_NONFINITE  =  -9,   /* 数值为 NaN/Inf */
    SVG_E_FLAGS      = -10,   /* 未知标志位 */
    SVG_E_ARC        = -11,   /* 椭圆弧参数求解失败（数值退化） */
    SVG_E_IO         = -12    /* 文件读写失败 */
};

/* 最近一次错误的详情。value 无对应数值时为 NAN。 */
typedef struct {
    int    code;
    int    seg;        /* 0 基段号；-1 表示不针对某一段 */
    char   field[16];  /* 出错字段名；无则为空串 */
    double value;      /* 出错数值，无则为 NAN */
    char   msg[112];   /* 人类可读描述 */
} SvgError;

int svg_last_error(void);
const SvgError *svg_last_error_detail(void);
const char *svg_strerror(int code);

/* ------------------------------------------------------------------ *
 * 椭圆弧参数化（SVG 1.1 附录 F.6.5）
 *
 * 按 rx、ry、xrot 与起止点解出圆心 (cx,cy)、起始参数角 theta1、
 * 有符号扫过角 dtheta（sweep=1 时 dtheta>0，sweep=0 时 dtheta<0）。
 * large 与 sweep 的组合决定取两组候选圆心中的哪一个。
 * theta2 = theta1 + dtheta，不折回 [-π,π]，便于直接观察扫过方向。
 *
 * 半径不足（Λ>1，圆弧够不到两个端点）按本模块约定报 SVG_E_RADIUS，
 * field="rx/ry"、value=所需放大系数；但 out 仍按 F.6.6 放大后的半径
 * 填好，供调用者诊断或自行决定是否采用。Λ<=1+1e-9 视为足够，
 * 避免正好半圆时浮点误差误报。
 *
 * 返回 SVG_OK 或负错误码；seg 仅用于错误定位，可传 0。
 * 该函数可对已解析的弧段单独调用，用来核对圆心与起止角。
 * ------------------------------------------------------------------ */
typedef struct {
    VPoint center;
    double rx, ry;     /* 生效半径（可能已放大） */
    double scale;      /* 放大系数 >= 1；1 表示半径足够 */
    double xrot;       /* 弧度，[-π/2, π/2] */
    double theta1;     /* 起始参数角，弧度 */
    double dtheta;     /* 有符号扫过角，弧度 */
    double theta2;     /* theta1 + dtheta */
} SvgArcFrame;

int svg_arc_frame(int seg, const VSeg *s, SvgArcFrame *out);

/* ------------------------------------------------------------------ *
 * 折线与容差
 *
 * 每段弦按参数角均分：n = ceil(|dtheta| / hmax)，
 *     hmax = sqrt(8*tol / rmax)，rmax = max(生效 rx, 生效 ry)
 * 于是弦上点满足
 *     | 到圆心距离 - 该方向椭圆半径 | <= rmax*(|dtheta|/n)^2/8 <= tol
 * 即用 max(rx,ry) 换掉逐点曲率半径，得到一个统一直观的上界；
 * 返回的 bound 是该上界的实际值（n 被 SVG_ARC_CHORD_CAP 截断时会 > tol）。
 *
 * 两档计数：
 *   nplus  = ceil(q)         余量档，实测径向偏差不超过上界；
 *   nminus = max(1, floor(q)) 紧凑档，少一段，偏差约放大 (n/(n-1))^2 倍，
 *                             可能略超上界。q = |dtheta|/hmax。
 * 两者随 bound 一并给出，便于对比“刚好够”和“留一点余量”的代价。
 *
 * svg_arc_polyline 输出 n+1 个弦端点，pts[0]=弧起点 a、pts[n]=弧终点 b
 * （与相邻段严格相接），容量至少 n+1。tol<=0 或非有限值返回 SVG_E_ARG。
 * ------------------------------------------------------------------ */
int svg_arc_chords(int seg, const VSeg *s, double tol,
                   int *n, double *bound, int *nplus, int *nminus);
int svg_arc_polyline(int seg, const VSeg *s, double tol,
                     VPoint *pts, int cap, int *n, double *bound);

/* ------------------------------------------------------------------ *
 * 填充
 *
 * 旧接口 svg_fill 的签名与返回值不变。对只含 M/L/Z 的路径，输出与改动前
 * 逐字节相同（交叉奇偶规则与浮点运算顺序均未动，见 compat 目标）。
 *
 * 弧段在兼容模式 SVG_ARC_CIRCLE 下“只会画整圆”：以弧起点 a 为圆心、
 * rx 为半径画整圆，忽略 ry/xrot/large/sweep 与弧终点，且不检查椭圆参数
 * 能否连接起止点。这就是开关为假时的画面，svg_fill 与
 * svg_fill_ex(..., SVG_ARC_CIRCLE, ...) 都走它。
 *
 * 打开 SVG_ARC_ELLIPSE 才按解出的圆心、起止角与扫过方向细分：
 *   SVG_ARC_ELLIPSE     余量档（nplus）
 *   SVG_ARC_ELLIPSE_MIN 紧凑档（nminus）
 *
 * 填充仍用逐行交叉奇偶规则，弧被展开成折线弦后逐弦计数。
 * tol<=0 或非有限值时用 SVG_DEFAULT_TOL。越界 arc_mode 返回 SVG_E_FLAGS。
 * ------------------------------------------------------------------ */
#define SVG_ARC_CIRCLE      0u   /* 兼容：弧段画整圆（圆心 a，半径 rx） */
#define SVG_ARC_ELLIPSE     1u   /* 参数化椭圆弧，余量档 */
#define SVG_ARC_ELLIPSE_MIN 2u   /* 参数化椭圆弧，紧凑档 */

#define SVG_MAX_ARCS 256
#define SVG_EDGE_CAP (1L << 20)

typedef struct {
    int    seg;         /* 段号 */
    int    whole;       /* 1 = 整圆兼容画法，角度字段为整圆 */
    VPoint center;
    double rx, ry;      /* 生效半径 */
    double xrot;        /* 弧度 */
    double theta1;
    double dtheta;
    double theta2;
    double bound;       /* 本段径向偏差上界；whole 时为 0 */
    int    nplus, nminus;
    int    n;           /* 本次实际弦数；whole 时为 0 */
} SvgArcRow;

typedef struct {
    int    arcs;        /* 弧段数 */
    long   chords;      /* 本次展开的弦总数 */
    double tol;         /* 实际使用的容差 */
    int    nplus, nminus;   /* 各段两档计数之和，供对照 */
    double max_bound;   /* 各段上界的最大值 */
    int    whole;       /* 1 = 兼容整圆模式 */
} SvgFillStats;

int svg_fill(const VPath *p, int w, int h, unsigned char *buf);
int svg_fill_ex(const VPath *p, int w, int h, unsigned char *buf,
                unsigned arc_mode, double tol, SvgFillStats *stats);

/* 最近一次 svg_fill_ex 的逐弧记录（最多 SVG_MAX_ARCS 条）。 */
int svg_arc_log_count(void);
const SvgArcRow *svg_arc_log(void);

int svg_save_pgm(const char *path, const unsigned char *buf, int w, int h);

#endif
