#ifndef MERGE_H
#define MERGE_H

typedef struct { double x, y; } MPoint;

#define MERGE_RING_CAP 256
#define MERGE_MAX_RINGS 8

typedef struct {
    int count;
    MPoint pts[MERGE_RING_CAP];
} MRing;

typedef struct {
    int n_rings;                 /* 结果环数（并集外环；关闭并集时为两段原值） */
    MRing rings[MERGE_MAX_RINGS];
    int seg_a, seg_b;            /* 本次拼接的两段编号（回显） */
    int union_on;                /* 实际生效的并集开关 */
    int n_hits;                  /* 切分用交点（切点）个数 */
    MPoint hits[MERGE_RING_CAP];
} MMerge;

/* 返回码 */
#define MERGE_OK 0
#define MERGE_E_ARG   -1         /* 空指针或点数不足 */
#define MERGE_E_WIND  -2         /* 两段绕序冲突（必须同为逆时针） */
#define MERGE_E_TANGENT -3       /* 切点重合：交点处幸存边无法唯一续接 */
#define MERGE_E_CAP   -4         /* 结果超出固定容量 */

/*
 * 拼接两段多边形。
 *   a/na, b/nb : 两段多边形顶点（闭合环，首尾不重复）
 *   id_a, id_b : 两段编号，出错信息中回带
 *   union_on   : 非零 -> 切分重叠并串并集外环；0 -> 不做并集，原样返回两段
 *   out        : 结果
 */
int merge_polygons(const MPoint *a, int na, int id_a,
                   const MPoint *b, int nb, int id_b,
                   int union_on, MMerge *out);

double merge_ring_area(const MRing *r);
double merge_union_area(const MMerge *m);

/* 取最近一次错误的说明（含两段编号），静态缓冲，非线程安全 */
const char *merge_last_error(void);

#endif
