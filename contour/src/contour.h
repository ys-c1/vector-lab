#ifndef CONTOUR_H
#define CONTOUR_H

#include <stddef.h>

#define CT_MAX_LAYERS 64

typedef struct { double x, y; } CTPoint;

/* 错误码：所有带层号的拒绝，层号写进 CTError.layer */
enum {
    CT_OK = 0,
    CT_ERR_IO = -1,        /* 文件打不开；layer = -1 */
    CT_ERR_FORMAT = -2,    /* 格式错误；layer = 当前层（-1 表示尚未进层）*/
    CT_ERR_TOOFEW = -3,    /* 该层不足三个点；layer = 层号 */
    CT_ERR_COLLINEAR = -4, /* 该层点全共线（含全重合）；layer = 层号 */
    CT_ERR_GAP = -5,       /* 层号跳号/乱序；layer = 期望出现的层号 */
    CT_ERR_CAP = -6,       /* 超过 CT_MAX_LAYERS；layer = 期望层号 */
    CT_ERR_MEM = -7,       /* 堆内存超过输入 6 倍；layer = -1 */
    CT_ERR_MONO = -8       /* 面积未随层号单调不增；layer = 第一个违例层 */
};

typedef struct { int code; int layer; } CTError;

typedef struct {
    int layer;
    int count;
    CTPoint *pts; /* 指向 CTSet 内统一池，不单独持有内存 */
} CTContour;

typedef struct {
    int layers;
    long input_bytes;      /* 输入文件字节数 */
    long peak_bytes;       /* 本模块堆内存峰值（文件缓冲 + 点池）*/
    char *text;            /* 整文件缓冲 */
    CTPoint *pool;         /* 一次性按实际顶点数分配的点池 */
    size_t pool_n;
    CTContour cs[CT_MAX_LAYERS];
} CTSet;

/* 解析多层文件。格式：
 *   L <层号>          层头，层号必须从 0 起连续递增
 *   x y               顶点行，每层至少 3 个点
 *   空行跳过；其余写法一律 CT_ERR_FORMAT
 * 成功返回 0，失败返回负错误码并填 err（含层号）。*/
int contour_load(const char *path, CTSet *set, CTError *err);

/* 逐层层校验：不足三点 / 全共线拒绝，并指出层号 */
int contour_validate(const CTSet *set, CTError *err);

/* 鞋带公式面积（与 gold 的数格子算法独立）*/
double contour_area(const CTPoint *pts, int n);

/* 面积序列必须随层号单调不增；违例时 err->layer 为第一个违例层 */
int contour_check_mono(const CTSet *set, const double *areas, CTError *err);

void contour_free(CTSet *set);
const char *ct_strerror(int code);

#endif
