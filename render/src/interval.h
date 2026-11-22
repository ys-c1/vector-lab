#ifndef RINTERVAL_H
#define RINTERVAL_H
/*
 * 向外舍入的双精度区间算术。
 *
 * 每次运算先用 fesetround 做有向舍入，再把两个端点各向外扩 1 ulp：
 * 硬件有向舍入对部分结果（如 10/64）两端相同，不扩 ulp 会得到假零宽度
 * 区间。扩 ulp 后真值必落在 [lo,hi] 内。
 *
 * 截断（IVL_TRUNC）只在严格非零的操作数区间做乘/除时、硬件置位
 * FE_UNDERFLOW 且结果出现零端点才判定：精确乘积已知非零却被舍成零。
 * 跨零区间（如输入 0 的 ±1 ulp 模型）被舍到零是合法包围，不算截断。
 *
 * 舍入模式按次切换，非线程安全；原浮点直算路径不调用本文件任何函数，
 * 渲染行为与编译产物不受影响。
 */
typedef struct { double lo, hi; } Ival;

typedef enum {
    IVL_OK = 0,        /* 区间有效 */
    IVL_TRUNC = 1,     /* 严格非零值下溢到零，信息被截断 */
    IVL_NONFINITE = 2, /* 端点出现 inf/NaN */
    IVL_DIVZERO = 3    /* 除数区间包含 0 */
} IvalStatus;

Ival iv_s(double x);              /* 精确单值 */
double iv_width(const Ival *a);   /* hi - lo */
int iv_finite(const Ival *a);

IvalStatus iv_neg(const Ival *a, Ival *out);
IvalStatus iv_add(const Ival *a, const Ival *b, Ival *out);
IvalStatus iv_sub(const Ival *a, const Ival *b, Ival *out);
IvalStatus iv_mul(const Ival *a, const Ival *b, Ival *out);
IvalStatus iv_div(const Ival *a, const Ival *b, Ival *out);
IvalStatus iv_sin(const Ival *a, Ival *out);
IvalStatus iv_cos(const Ival *a, Ival *out);

const char *iv_strerror(IvalStatus st);
#endif
