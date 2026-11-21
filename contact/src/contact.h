#ifndef CONTACT_H
#define CONTACT_H

#include <stdio.h>

/*
 * 三次贝塞尔曲线与线段的接触分类：分清“穿过”(cross) 与“擦边”(graze)。
 *
 * 做法（逐个采样区间）：
 *   1. 先把曲线按 spans 段均匀采样成折线；对每个区间取两端 p0、p1 与中点
 *      pm，先算区间弦方向与线段方向的叉积 chord_cross（与 isect_seg 的
 *      den 同式）。弦与线段平行时只可能共线贴合，不产生孤立交点。
 *   2. 取三点相对线段所在直线的有符号距离 h0、hm、h1（按线段长度归一化），
 *      拟合二次式 h(tau) = A*tau^2 + B*tau + C：
 *          A = 2*(h0 - 2*hm + h1)
 *          B = 4*hm - 3*h0 - h1
 *          C = h0
 *      判别式 D = B^2 - 4*A*C。
 *   3. 按判别式分辨：
 *          D > 带宽       -> 两个实根；根两侧曲线真的换边判“穿过”，
 *                            不换边且贴近线段判“擦边”；
 *          |D| <= 带宽    -> 重根，相切（擦边），给出切点参数 t / u；
 *          D < -带宽      -> 该区间无实根，候选被拒绝；分类表对应行
 *                            reason=discriminant_negative、field=discriminant。
 *      带宽 = 4*|A|*tol，等价于判别式顶点到线段距离 |h_vertex| <= tol；
 *      另加 1e-12*(B^2+|4AC|) 吸收浮点噪声。
 *   4. 二次模型顶点落在线段上且 |h| <= tol 也判擦边；曲线端点、线段端点
 *      接触与共线贴合一律归入擦边，并在 reason 中注明。
 *
 * 关闭分类（flags 不含 CT_F_CLASSIFY）时走旧路径：固定 8 段采样，
 * 逐弦套用与 isect/src/isect.c 的 isect_seg 完全相同的叉积求交算式，
 * 命中参数逐位一致；此时忽略 spans，输出即“与现在一致”。
 *
 * 接触分类表（逐区间 rows[]）与合并后的接触点表（hits[]）均可导出 CSV。
 */

typedef struct { double x, y; } CtPoint;

#define CT_MAX_SPANS 64
#define CT_MAX_HITS  128
#define CT_FIELD_MAX 32
#define CT_LEGACY_SPANS 8   /* 现状：bezier_flatten 固定 8 段 */

/* 返回码；失败时 err_field 给出出错字段名（可为 NULL）。 */
enum {
    CT_OK = 0,
    CT_E_ARG = -1,    /* 空指针 / spans、span 越界；field = "c"/"spans"/"span"/"out" */
    CT_E_CTRL = -2,   /* 控制点坐标非有限；field = "c[k].x" / "c[k].y" */
    CT_E_SEG = -3,    /* 线段端点非有限或零长；field = "b0"/"b1"/"b0/b1" */
    CT_E_TOL = -4,    /* tol 为负或非有限；field = "tol" */
    CT_E_FLAGS = -5,  /* 未知标志位；field = "flags" */
    CT_E_DISC = -6,   /* 判别式为负（严格模式 / 单区间）；field = "discriminant" */
    CT_E_CAP = -7,    /* 接触点超过 CT_MAX_HITS */
    CT_E_IO = -8      /* CSV 写出失败 */
};

#define CT_F_CLASSIFY    1u  /* 打开穿过/擦边分类 */
#define CT_F_STRICT_DISC 2u  /* 任一区间判别式为负即整体返回 CT_E_DISC */

/* 行状态 / 接触点类型 */
enum {
    CT_KIND_NONE = 0,
    CT_KIND_CROSS = 1,   /* 穿过 */
    CT_KIND_GRAZE = 2,   /* 擦边（相切 / 端点接触 / 共线贴合） */
    CT_KIND_REJECT = 3   /* 候选被拒绝（含判别式为负） */
};

/* 分类依据 / 拒绝原因 */
enum {
    CT_R_NONE = 0,
    CT_R_CROSS,              /* 两个实根且根两侧换边 */
    CT_R_TANGENT,            /* 判别式近零（重根）或顶点贴近线段 */
    CT_R_CURVE_ENDPOINT,     /* 接触点落在曲线端点 t=0/1 */
    CT_R_SEGMENT_ENDPOINT,   /* 接触点落在线段端点 u=0/1 */
    CT_R_COINCIDENT,         /* 弦与线段共线贴合 */
    CT_R_PARALLEL,           /* 弦与线段平行且远离，无接触 */
    CT_R_DISC_NEG,           /* 判别式为负，区间无实根 */
    CT_R_U_OUTSIDE,          /* 交点在线段参数之外 */
    CT_R_ROOT_OUTSIDE,       /* 实根落在区间之外 */
    CT_R_MODEL_MISS,         /* 二次模型根处曲线仍不贴近线段 */
    CT_R_LEGACY              /* 关闭分类：旧求交路径 */
};

/* 合并后的接触点 */
typedef struct {
    int kind;        /* CT_KIND_CROSS / CT_KIND_GRAZE */
    int span;        /* 来源区间号（旧路径为弦号） */
    double t;        /* 曲线参数 [0,1] */
    double u;        /* 线段参数 [0,1] */
    CtPoint at;      /* 接触点 */
    double disc;     /* 来源区间的判别式；旧路径为 NAN */
    double resid;    /* 接触点到线段所在直线的有符号距离 */
    int reason;      /* CT_R_* */
} CtHit;

/* 分类表的一行 = 一个采样区间 */
typedef struct {
    int span;
    int status;         /* CT_KIND_NONE / CROSS / GRAZE / REJECT */
    double A, B, C;     /* 二次模型系数 */
    double disc;        /* 判别式 B^2 - 4AC */
    double chord_cross; /* 先算的：区间弦方向 × 线段方向 */
    int chord_hit;      /* 该弦是否与线段本体相交（叉积粗筛结果） */
    double h0, hm, h1;  /* 区间两端与中点的有符号距离 */
    double t, u;        /* 接触或最接近点参数；无候选为 NAN */
    CtPoint at;         /* 对应点；无候选为 (NAN,NAN) */
    double resid;       /* 接触点到线段所在直线的有符号距离；无候选为 NAN */
    int reason;         /* CT_R_* */
    const char *field;  /* 拒绝时出错字段（"discriminant"/"u"），否则空串 */
} CtRow;

typedef struct {
    int spans;           /* 本次扫描段数；旧路径固定 CT_LEGACY_SPANS */
    int classified;      /* 0 = 旧求交路径，1 = 分类路径 */
    int nrows;           /* 分类表行数；旧路径为 0 */
    CtRow rows[CT_MAX_SPANS];
    int count;           /* 合并后的接触点个数 */
    CtHit hits[CT_MAX_HITS];
    int crosses;         /* 穿过个数 */
    int grazes;          /* 擦边个数 */
    int rejects;         /* 被拒绝的区间数 */
    int disc_neg;        /* 判别式为负的区间数 */
    int raw;             /* 合并前候选数 */
    int merged;          /* 合并掉的重复候选数 */
} CtScan;

/* 旧路径的逐弦命中，字段与 isect_seg 一一对应 */
typedef struct { int chord; double t; double u; CtPoint at; } CtChordHit;

/*
 * 曲线与线段接触扫描。
 *   c         4 个三次贝塞尔控制点
 *   b0, b1    查询线段（零长拒绝）
 *   spans     采样段数 1..CT_MAX_SPANS；仓库基准用 8 与 64。
 *             关闭分类时忽略此参数（固定 8 段以复现现状）。
 *   flags     0 = 旧求交路径；CT_F_CLASSIFY = 分类；
 *             CT_F_CLASSIFY|CT_F_STRICT_DISC = 分类且判别式为负即返回错误
 *   tol       擦边容差（距离单位，>= 0）；关闭分类时忽略但仍校验
 *   out       结果；可为 NULL，此时只做输入校验
 *   err_field 出错字段名（可为 NULL，容量至少 CT_FIELD_MAX）
 * 返回 CT_OK 或负错误码。
 */
int ct_contact(const CtPoint c[4], CtPoint b0, CtPoint b1,
               int spans, unsigned flags, double tol,
               CtScan *out, char err_field[CT_FIELD_MAX]);

/*
 * 旧路径逐弦求交（不分类）。spans = 8 时与仓库现有
 * bezier_flatten(8 段) + isect_seg 的命中逐位一致。
 * hits 可为 NULL（只计数）；cap 为 hits 容量。
 * 返回命中数（>= 0）或负错误码；命中数超过 cap 时只写前 cap 条。
 */
int ct_legacy_hits(const CtPoint c[4], CtPoint b0, CtPoint b1, int spans,
                   CtChordHit *hits, int cap, char err_field[CT_FIELD_MAX]);

/*
 * 只分类一个区间，便于逐段审计。
 * 该区间判别式为负时返回 CT_E_DISC，err_field = "discriminant"，
 * 同时仍把 A/B/C/D、h0/hm/h1 写入 out 供定位。
 */
int ct_classify_span(const CtPoint c[4], CtPoint b0, CtPoint b1,
                     int spans, int span, double tol,
                     CtRow *out, char err_field[CT_FIELD_MAX]);

/* 导出逐区间分类表 / 合并后的接触点表为 CSV。返回 CT_OK 或 CT_E_*。 */
int ct_export_rows(const CtScan *scan, FILE *fp);
int ct_export_hits(const CtScan *scan, FILE *fp);

const char *ct_kind_name(int kind);
const char *ct_reason_name(int reason);
const char *ct_strerror(int code);

#endif
