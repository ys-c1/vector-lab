#include "xform.h"

/* 块宽默认值：256 点。稠密时每块 4 KiB，处于 L1d(48 KiB) 内、
 * 并覆盖全部 12 路组相联，详见 perf/tuning.md 的推导。*/
#define XFORM_BLOCK_DEFAULT 256u

static double load_x(const void *p, size_t stride, size_t i) {
    const unsigned char *q = (const unsigned char *)p + (size_t)i * stride;
    double v;
    __builtin_memcpy(&v, q, sizeof(v));
    return v;
}
static double load_y(const void *p, size_t stride, size_t i) {
    const unsigned char *q = (const unsigned char *)p + (size_t)i * stride;
    double v;
    __builtin_memcpy(&v, q + sizeof(double), sizeof(v));
    return v;
}
static void store_xy(void *p, size_t stride, size_t i, double x, double y) {
    unsigned char *q = (unsigned char *)p + (size_t)i * stride;
    __builtin_memcpy(q, &x, sizeof(x));
    __builtin_memcpy(q + sizeof(double), &y, sizeof(y));
}

void xform_apply_batch_strided(XForm m, void *base, size_t n,
                               size_t stride, size_t w) {
    if (base == 0 || n == 0 || stride < 2 * sizeof(double))
        return;
    if (w == 0)
        w = XFORM_BLOCK_DEFAULT;

    size_t off = 0;
    while (off < n) {
        size_t end = off + w;
        if (end > n || end < off)   /* 末块截断，同时防 size_t 回绕 */
            end = n;
        for (size_t i = off; i < end; ++i) {
            double x = load_x(base, stride, i);
            double y = load_y(base, stride, i);
            double rx = m.a * x + m.c * y + m.e;
            double ry = m.b * x + m.d * y + m.f;
            store_xy(base, stride, i, rx, ry);
        }
        off = end;
    }
}

void xform_apply_batch(XForm m, XPoint *pts, size_t n, size_t w) {
    xform_apply_batch_strided(m, pts, n, sizeof(*pts), w);
}

void xform_apply_batch_copy(XForm m, const XPoint *src, XPoint *dst,
                            size_t n, size_t w) {
    if (src == 0 || dst == 0 || n == 0)
        return;
    if (w == 0)
        w = XFORM_BLOCK_DEFAULT;

    size_t off = 0;
    while (off < n) {
        size_t end = off + w;
        if (end > n || end < off)
            end = n;
        for (size_t i = off; i < end; ++i) {
            double x = src[i].x;
            double y = src[i].y;
            dst[i].x = m.a * x + m.c * y + m.e;
            dst[i].y = m.b * x + m.d * y + m.f;
        }
        off = end;
    }
}
