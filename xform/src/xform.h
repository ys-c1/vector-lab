#ifndef XFORM_H
#define XFORM_H
#include <stddef.h>
typedef struct { double a, b, c, d, e, f; } XForm;
typedef struct { double x, y; } XPoint;
XForm xform_identity(void);
XPoint xform_apply(XForm m, XPoint p);
XForm xform_mul(XForm m, XForm n);

/*
 * 批量原地仿射变换（分块）。
 *
 * base   : 点缓冲区首地址；每个点的 (x,y) 两个 double 位于该点步长的起始 16 字节
 * n      : 点数
 * stride : 点步长（字节）。稠密数组传 sizeof(XPoint)=16；稀疏数组可传 64 等
 * w      : 块宽（每块点数），w==0 时取内部默认值
 *
 * 语义保证：
 *  - 原地执行：同一地址先读后写，调用方只需持有一份缓冲区；
 *  - 函数内不调用 malloc/calloc/realloc/aligned_alloc，不调用 memcpy/memmove，
 *    不存在第二次拷贝，峰值内存就是输入本身（外加 O(1) 栈空间）；
 *  - NaN 按 IEEE 754 自然传播（乘法/加法NaN规则），无分支特判，
 *    因此编译不得启用 -ffast-math。
 */
void xform_apply_batch_strided(XForm m, void *base, size_t n,
                               size_t stride, size_t w);

/* 稠密便捷封装：stride = sizeof(XPoint) */
void xform_apply_batch(XForm m, XPoint *pts, size_t n, size_t w);

/*
 * 对照实现（二次拷贝）：dst 由调用方另行分配，src 只读。
 * 仅用于内存占用与带宽流量的对照测量，生产路径不要使用。
 */
void xform_apply_batch_copy(XForm m, const XPoint *src, XPoint *dst,
                            size_t n, size_t w);
#endif
