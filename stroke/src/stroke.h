#ifndef STROKE_H
#define STROKE_H

#include <stdio.h>

/* 旧接口容量：每条边一对偏移点，最多写入 126 点。 */
typedef struct { double x, y; } SPoint;
typedef struct { int count; SPoint pts[128]; } SPoly;

/* 旧接口：逐点行为保持不变。 */
int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out);

/* ------------------------------------------------------------------ *
 * 自交检测与内翻环裁剪
 *
 * stroke_polyline 的输出是每个顶点一对偏移点直接相连的“交叉梯子”，
 * 拐角处相邻两对偏移点会交叉，轮廓自己绕进去。本组接口：
 *   1. 按原始边两两求交，得到可编号的交点表；
 *   2. 把交点插入边序列，用栈式环分解把闭环拆成若干简单环；
 *   3. 与主环同向的环保留，反向绕入的环逐个裁掉并编号。
 * 分解满足：原始面积 = 保留面积 + 裁掉面积（误差仅为浮点舍入）。
 * kept_area 是各保留环有向面积之和；线宽较大、同一处被绕两圈时，
 * 保留环可能互相覆盖，此时它不等于真实带宽的并集面积（并集需另做
 * 绕数合并）。这一点在基准中以 kept 环两两不加合并来体现。
 *
 * 不裁环（cut = 0）时输出与 stroke_polyline 逐点一致。
 * 交点为端点或两段共线重叠时返回 STROKE_E_ENDPOINT / STROKE_E_COLLINEAR，
 * 并通过 bad_pair 指出交点对号（0 基，按段对 (i,j) 字典序编号）。
 * ------------------------------------------------------------------ */

#define STROKE_MAX_RAW      256    /* 待分解轮廓的最大点数 */
#define STROKE_MAX_HITS    2048    /* 交点表容量 */
#define STROKE_MAX_LOOPS   2048    /* 简单环总数上限 */
#define STROKE_MAX_PAIRS     16    /* 单个环记录的关联交点对号上限 */
#define STROKE_POOL_PTS    8192    /* 环顶点池：等于闭环序列总长上限 */

enum {
    STROKE_HIT_PROPER    = 1,   /* 两段内部相交 */
    STROKE_HIT_ENDPOINT  = 2,   /* 交点在某一端点上 */
    STROKE_HIT_COLLINEAR = 3    /* 共线且重叠长度大于容差 */
};

enum {
    STROKE_OK          =  0,
    STROKE_E_ARG       = -1,    /* 参数非法 */
    STROKE_E_ZERO_EDGE = -2,    /* 零长度边 */
    STROKE_E_ENDPOINT  = -3,    /* 交点为端点，见 bad_pair */
    STROKE_E_COLLINEAR = -4,    /* 共线重叠，见 bad_pair */
    STROKE_E_CAP       = -5,    /* 容量不足 */
    STROKE_E_INTERNAL  = -6     /* 平面图内部不一致 */
};

typedef struct {
    int    pair;        /* 交点对号，0 基 */
    int    ea, eb;      /* 原始轮廓边号，ea < eb */
    int    kind;        /* STROKE_HIT_* */
    double ta, tb;      /* 参数，[0,1] */
    SPoint at;          /* 交点坐标 */
} SStrokeHit;

/* 保留环：顶点是 result->points[off .. off+count)，edges[off+k] 为该顶点
 * 所在的原始边号（交点顶点取两条原边中较小者）。 */
typedef struct {
    int    loop_id;     /* 环在分解中的编号，0 基 */
    int    count;
    int    off;
    double area;        /* 有向面积，与主环同号 */
    int    first_pair;  /* 环上最小交点对号；无交点为 -1 */
} SStrokeRing;

/* 被裁掉的内翻环，index 从 1 开始逐个编号。 */
typedef struct {
    int    index;       /* 裁掉环号，1 基 */
    int    loop_id;     /* 分解中的环号 */
    int    count;
    int    off;
    double area;        /* 按环走向的有向面积，与主环异号 */
    int    pair;        /* 代表交点对号；无交点为 -1 */
    int    ea, eb;      /* 该交点对所在原始边 */
    SPoint at;          /* 代表切点（pair 的交点坐标） */
    int    hit_pair_count;   /* hit_pairs 中实际写入的条数 */
    int    hit_pair_total;   /* 环上交点对总数（可能大于写入条数） */
    int    hit_pairs[STROKE_MAX_PAIRS]; /* 环上部分交点对号，升序 */
} SStrokeLoop;

typedef struct {
    int status;         /* STROKE_* */
    int degenerate;     /* 0 或最先遇到的 STROKE_HIT_ENDPOINT/COLLINEAR */
    int bad_pair;       /* 出问题的交点对号，无则 -1 */
    int raw_simple;     /* 1：没有自交点 */

    int raw_count;
    SPoint raw[STROKE_MAX_RAW];
    double raw_area;

    int hit_count;
    SStrokeHit hits[STROKE_MAX_HITS];

    int kept_count;     /* 保留环条数 */
    SStrokeRing kept[STROKE_MAX_LOOPS];

    int loop_count;     /* 裁掉环条数（逐个编号 1..loop_count） */
    SStrokeLoop loops[STROKE_MAX_LOOPS];
    int total_loop_count; /* 分解出的简单环总数 = kept_count + loop_count */

    int point_count;
    SPoint points[STROKE_POOL_PTS];
    int    edges[STROKE_POOL_PTS];

    double kept_area;
    double removed_area;
    double closure_error; /* raw_area - kept_area - removed_area */
} SStrokeResult;

/* 对一条闭合轮廓做求交与裁环。
 * cut = 0：不裁环，kept[0] 与输入逐点一致；退化交点只记录，不返回错误。
 * cut != 0：裁掉反向绕入的环；退化交点返回负错误码。 */
int stroke_resolve_polygon(const SPoint *poly, int n, int cut, SStrokeResult *out);

/* 先用 stroke_polyline 生成轮廓，再调用 stroke_resolve_polygon。 */
int stroke_resolve(const SPoint *pts, int n, double width, int cut,
                   SStrokeResult *out);

/* 交点表导出 CSV（列：pair,edge_a,edge_b,kind,ta,tb,x,y），返回写入字节数，
 * 失败返回负值。用于与手算交点表逐行核对。 */
long stroke_hits_csv(const SStrokeResult *r, FILE *fp);

/* 裁掉环表导出 CSV（列：index,loop_id,count,area,pair,edge_a,edge_b,
 * x,y,hit_pair_count,hit_pair_total,hit_pairs），返回写入字节数。 */
long stroke_loops_csv(const SStrokeResult *r, FILE *fp);

const char *stroke_strerror(int code);

#endif
