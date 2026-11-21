#ifndef STROKE_H
#define STROKE_H

/* 最坏情形：STROKE_MAX_IN 个全锐角拐点，圆角每角每侧约 24 点，
 * 双侧约 12.3k 点，16384 足以覆盖全部合法输入 */
#define STROKE_MAX_PTS 16384
#define STROKE_MAX_IN   256

typedef struct { double x, y; } SPoint;
typedef struct { int count; SPoint pts[128]; } SPoly;

/* 旧接口：逐点行为保持不变，等价于 SJ_LEGACY */
int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out);

/* 接合策略 */
#define SJ_AUTO   0   /* 按夹角择优：钝角斜接 / 直角斜切 / 尖角圆角 */
#define SJ_MITER  1   /* 斜接 */
#define SJ_BEVEL  2   /* 斜切 */
#define SJ_ROUND  3   /* 圆角 */
#define SJ_LEGACY 4   /* 旧策略：每顶点一对偏移点直接相连 */

/* AUTO 分档阈值（弧度，均为路径夹角 gamma） */
#define SJ_MITER_ANGLE 2.0943951023931953  /* 120°：gamma >= 它且不超限 -> 斜接 */
#define SJ_ROUND_ANGLE 1.0471975511965976  /*  60°：gamma <  它          -> 圆角 */
#define SJ_DEFAULT_MITER_LIMIT 4.0
#define SJ_ROUND_STEP 0.13962634015954636  /* 圆弧细分步长 ~8° */
#define SJ_EPS 1e-9

/* 每个拐点的选择记录 */
typedef struct {
    int    index;        /* 拐点号，0 基（顶点下标） */
    double angle;        /* 路径夹角 gamma，[0, pi] */
    int    join;         /* 实际采用的接法 SJ_* */
    double miter_ratio;  /* 斜接长/线宽，直拐角为 1 */
    int    rejected;     /* 非 0：该拐点拒绝生成轮廓 */
    int    reason;       /* 0 正常；-3 夹角为零；-4 斜接超限 */
} SJoinInfo;

typedef struct {
    int    join;             /* SJ_AUTO / SJ_MITER / ... */
    double miter_limit;      /* 斜接长/线宽上限，<=0 取默认 4.0 */
    int    record_count;     /* 输出：写入 info 的条数（= n-2） */
    SJoinInfo info[STROKE_MAX_IN];
} SStrokeOpt;

/* 大容量轮廓（圆角细分后点数可能很多） */
typedef struct { int count; SPoint pts[STROKE_MAX_PTS]; } SOutline;

/*
 * 返回值：
 *   0  成功
 *  -1  参数非法
 *  -2  存在零长度边
 *  -3  夹角为零的拐点
 *  -4  斜接长度超过 miter_limit 的拐点
 *  -5  输出缓冲不足
 * 出错时 *bad_index 为问题拐点号（0 基；-2 时为问题边的起点下标）。
 */
int stroke_outline(const SPoint *pts, int n, double width,
                   SStrokeOpt *opt, SOutline *out, int *bad_index);

#endif
