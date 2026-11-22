#ifndef ARCLEN_H
#define ARCLEN_H
typedef struct { double x, y; } APoint;
typedef struct { int count; double s[64]; } ATable;
int arclen_build(const APoint *pts, int n, ATable *out);
double arclen_total(const ATable *t, const APoint *pts, int n);
int arclen_sample_at(const APoint *pts, int n, const ATable *t, double s, APoint *out);

/* 等分点采样：先攒累计长度表，再按 k*spacing 反解每段内的参数。
 * 返回点数（>=0）或负错误码；out 为 NULL 时只返回所需点数（查询模式）。
 */
enum {
    ARCLEN_ERR_ARG = -1,        /* 参数非法（空指针、n<2、间距非正） */
    ARCLEN_ERR_TOTAL_ZERO = -2, /* 总长为零 */
    ARCLEN_ERR_SPACING = -3,    /* 间距比总长还大 */
    ARCLEN_ERR_SEGCOUNT = -4,   /* 段数小于二 */
    ARCLEN_ERR_CAP = -5         /* 输出容量不足 */
};
int arclen_even(const APoint *pts, int n, double spacing, APoint *out, int maxout);
const char *arclen_strerror(int code);
#endif
