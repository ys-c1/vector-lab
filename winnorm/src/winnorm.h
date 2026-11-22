#ifndef WINNORM_H
#define WINNORM_H
#include <stddef.h>

/* 窗口顶点绕序归一化。
 *
 * 窗口顶点常常是"顺手写"的：有的顺、有的逆，有的还自交。自交时鞋带公式
 * 的整环有符号面积会随顶点顺序时正时负，单看这个符号会骗人（蝴蝶结与
 * 双环的整环面积都恰为 0）。本模块的处理顺序：
 *
 *   1) 自交检测。只有两条非相邻边在各自内部真穿过才算自交；顶点落在
 *      别的边上、共线重叠单独分类；相邻边共用端点不算。
 *   2) 拆分。winnorm_split_self() 用平面排列（arrangement）拆：在所有真
 *      穿过处切开两条边，做半边结构、追踪面，按输入环绕数的奇偶保留
 *      填实的面。得到的子环两两互不相交，每个都是简单环；中心被覆盖
 *      两次的区域按奇偶规则是洞，不会被拆成一个多余的正向子环。
 *   3) 定向。按拆分后每个子环自己的有符号面积定方向：正为逆时针、负为
 *      顺时针、零为退化拒绝。符号只用来决定"要不要翻"，不用来相信输入。
 *   4) 统一绕序。需要翻转的子环整环逆序（首点不动），使每个子环有符号
 *      面积为正。逆序不改变顶点集合，几何不变。
 *
 * 面积绝对值恒不变：翻环只是把顶点序列倒过来，每个鞋带项的舍入误差与
 * 翻转前逐项相同（只是位置换了），|面积| 在数学上是同一个量。精确求和把
 * 正项、负项分开按 |值| 升序累加：逆序后两堆恰好整体取负互换，IEEE 加法
 * 对全操作数取负精确对称，因此翻前翻后的 |面积| 逐位相同，偏差恒为 0。
 * 不变量守护：翻转后再用同一套求和复算一遍，若与翻前的 |面积| 不是逐位
 * 相同，就返回 WN_ERR_AREA_DRIFT，而不是悄悄交出一个变了的结果。
 *
 * 拒绝规则（全部带序号）：
 *   - 重复点（相邻重合或任意两处重合）      -> WN_ERR_DUP_POINT, at = 点下标
 *   - 自交未拆分（allow_split=0）           -> WN_ERR_SELF_CROSS, at/at2 = 边号
 *   - 顶点贴边 / 共线重叠（拆分也消不掉）    -> WN_ERR_CONTACT, at/at2 = 边号
 *   - 子环面积为 0                           -> WN_ERR_DEGENERATE, at = 子环号
 */

#define WN_MAX_VERTS 256   /* 单个环的顶点上限 */
#define WN_MAX_RINGS 16    /* 子环上限 */
#define WN_MAX_CROSS 128   /* 接触分类表上限 */

#define WN_EPS 1e-9        /* 接触判定：绝对容差下限 */
#define WN_REL 1e-12       /* 接触判定：随坐标量级的相对容差 */

typedef struct { double x, y; } WPoint;

typedef struct {
    int n;
    WPoint pts[WN_MAX_VERTS];
} WRing;

typedef struct {
    int rings;                           /* 子环数 */
    WRing ring[WN_MAX_RINGS];            /* 归一化后（有符号面积恒 > 0） */
    double abs_area[WN_MAX_RINGS];       /* 子环面积绝对值，翻前翻后逐位相同 */
    double in_signed_area[WN_MAX_RINGS]; /* 拆开后、翻转前的有符号面积 */
    int flipped[WN_MAX_RINGS];           /* 该子环是否被逆序过 */
} WNorm;

/* 边对接触分类。 */
enum {
    WN_TOUCH_NONE = 0,
    WN_TOUCH_PROPER = 1,   /* 两条非相邻边在各自内部真穿过 */
    WN_TOUCH_VERTEX = 2,   /* 一条边的端点落在另一条边上 */
    WN_TOUCH_OVERLAP = 3   /* 共线且重叠 */
};

typedef struct {
    int edge_a, edge_b;    /* 边号，edge_a < edge_b（0 起） */
    int kind;              /* WN_TOUCH_* */
} WContact;

enum {
    WN_OK = 0,
    WN_ERR_NULL = -1,           /* 空指针/参数非法 */
    WN_ERR_TOO_FEW = -2,        /* 顶点不足三个 */
    WN_ERR_TOO_MANY = -3,       /* 顶点超过 WN_MAX_VERTS */
    WN_ERR_DUP_POINT = -4,      /* 有重复点，at = 重复点下标 */
    WN_ERR_SELF_CROSS = -5,     /* 自交未拆分，at/at2 = 两条边号 */
    WN_ERR_CONTACT = -6,        /* 残留顶点接触/共线重叠，at/at2 = 边号 */
    WN_ERR_DEGENERATE = -7,     /* 某子环面积为零，at = 子环号 */
    WN_ERR_TOO_MANY_RINGS = -8, /* 子环数超过 WN_MAX_RINGS */
    WN_ERR_CAP = -9,            /* 子环顶点数超过容量 */
    WN_ERR_AREA_DRIFT = -10     /* 不变量失败：翻转前后 |面积| 不再逐位相同 */
};

/* 有符号面积（鞋带公式，正为逆时针），普通 double 累加，仅用于报表。 */
double winnorm_signed_area(const WPoint *p, int n);

/* 有符号面积，符号分堆 + 升序累加的精确对称求和。翻环后 |结果| 与翻前
 * 逐位相同。 */
double winnorm_signed_area_exact(const WPoint *p, int n);

/* 逐对边分类接触。out 只写前 cap 条；返回值是真实条数（可能 > cap）。
 * 参数非法返回负错误码。 */
int winnorm_contacts(const WPoint *win, int n, WContact *out, int cap);

/* 只检测不归一化。
 * 返回 0 干净；1 有拒绝原因（out_code 为负错误码，at/at2 为涉事序号）；
 * -1 参数非法（out_code = WN_ERR_NULL）。*/
int winnorm_check(const WPoint *win, int n, int *out_code, int *at, int *at2);

/* 在真穿过处把自交窗口拆成简单子环（只切分，不定向）。
 * 返回子环数（>=1）；重复点/顶点贴边/共线重叠按错误码拒绝并给序号。*/
int winnorm_split_self(const WPoint *win, int n, WRing rings[], int cap,
                       int *at, int *at2);

/* 归一化一个已经简单的环：拒绝重复点、自交、接触与零面积，
 * 面积为负时整环逆序。abs_area 写入翻前翻后相同的面积绝对值。*/
int winnorm_normalize_ring(const WPoint *win, int n, WRing *ring,
                           double *abs_area, int *flipped,
                           int *at, int *at2);

/* 归一化一组已拆分的简单子环（不拆分，遇到自交直接拒）。*/
int winnorm_normalize_set(const WRing *rings, int nr, WNorm *out,
                          int *at, int *at2);

/* 一站式入口。
 * allow_split=0：输入必须已经简单，自交 -> WN_ERR_SELF_CROSS（给边号）。
 * allow_split!=0：先在真穿过处拆分，再逐子环定向、统一绕序。
 * 两条路径都不改变几何，面积绝对值逐位不变。*/
int winnorm_normalize(const WPoint *win, int n, int allow_split, WNorm *out,
                      int *at, int *at2);

const char *winnorm_strerror(int code);

#endif
