#include "xform.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* NaN 只能用 isnan 判定，不能用 == */
static int is_nan(double v) { return isnan(v); }

int main(void) {
    XForm id = xform_identity();
    XPoint p = {2, 3};
    XPoint q = xform_apply(id, p);
    CK(q.x == 2 && q.y == 3);
    XForm t = {1, 0, 0, 1, 10, 20};
    XPoint r = xform_apply(t, p);
    CK(r.x == 12 && r.y == 23);
    XForm s = {2, 0, 0, 2, 0, 0};
    XForm m = xform_mul(s, t);
    XPoint u = xform_apply(m, p);
    CK(u.x == 24 && u.y == 46);

    /* 批量稠密：结果必须与逐点 xform_apply 一致，含非整除块宽的末块截断 */
    XForm rot = {0.5, -0.25, 0.75, 1.5, 3.0, -4.0};
    enum { N = 1003 };
    static XPoint pts[N], ref[N];
    for (int i = 0; i < N; ++i) {
        pts[i].x = ref[i].x = 0.11 * i - 7.0;
        pts[i].y = ref[i].y = -0.37 * i + 2.0;
    }
    static const size_t widths[] = {1, 4, 7, 64, 100, 256, 1024, 0};
    for (size_t k = 0; k < sizeof(widths) / sizeof(widths[0]); ++k) {
        XPoint work[N];
        memcpy(work, ref, sizeof(work));
        xform_apply_batch(rot, work, N, widths[k]);
        for (int i = 0; i < N; ++i) {
            XPoint e = xform_apply(rot, ref[i]);
            CK(work[i].x == e.x && work[i].y == e.y);
        }
    }

    /* w >= n 与默认 w=0 路径等价 */
    {
        XPoint a[5], b[5];
        for (int i = 0; i < 5; ++i) { a[i] = b[i] = (XPoint){i + 1, i - 1}; }
        xform_apply_batch(rot, a, 5, 0);
        xform_apply_batch(rot, b, 5, 999);
        CK(memcmp(a, b, sizeof(a)) == 0);
    }

    /* 稀疏步长：64 字节一个槽，只动前 16 字节，槽内其余字节不得被改写 */
    enum { SN = 37 };
    static unsigned char buf[SN * 64];
    memset(buf, 0xA5, sizeof(buf));
    for (int i = 0; i < SN; ++i) {
        double *xy = (double *)(buf + (size_t)i * 64);
        xy[0] = (double)i;
        xy[1] = -2.0 * i;
    }
    xform_apply_batch_strided(t, buf, SN, 64, 16);
    for (int i = 0; i < SN; ++i) {
        double *xy = (double *)(buf + (size_t)i * 64);
        CK(xy[0] == (double)i + 10.0 && xy[1] == -2.0 * i + 20.0);
        unsigned char *tail = buf + (size_t)i * 64 + 16;
        for (int j = 0; j < 48; ++j)
            CK(tail[j] == 0xA5);
    }

    /* NaN 传播：输入含 NaN 时对应输出必为 NaN，且不污染相邻点 */
    {
        XPoint np[4] = {{1, 2}, {NAN, 4}, {5, NAN}, {7, 8}};
        xform_apply_batch(rot, np, 4, 4);
        CK(np[0].x == rot.a * 1 + rot.c * 2 + rot.e);
        CK(is_nan(np[1].x) && is_nan(np[1].y));   /* NaN 经乘/加传播 */
        CK(is_nan(np[2].x) && is_nan(np[2].y));
        CK(!is_nan(np[3].x) && !is_nan(np[3].y));
    }

    /* 对照（二次拷贝）实现与原地实现数值一致 */
    {
        XPoint src[11], dst[11], inplace[11];
        for (int i = 0; i < 11; ++i)
            src[i] = inplace[i] = (XPoint){1.5 * i, -0.5 * i};
        xform_apply_batch_copy(rot, src, dst, 11, 4);
        xform_apply_batch(rot, inplace, 11, 4);
        CK(memcmp(dst, inplace, sizeof(dst)) == 0);
        /* 对照实现必须保持 src 原样 */
        CK(src[7].x == 1.5 * 7 && src[7].y == -0.5 * 7);
    }

    /* 退化参数不崩溃 */
    xform_apply_batch(rot, 0, 0, 256);
    xform_apply_batch_strided(rot, pts, N, 8, 256); /* stride 过小：拒绝执行 */
    CK(pts[0].x == ref[0].x && pts[0].y == ref[0].y); /* 数据未被改动 */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
