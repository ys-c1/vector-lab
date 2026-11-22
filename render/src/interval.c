#include "interval.h"
#include <fenv.h>
#include <math.h>

/* GCC 不识别 STDC FENV 编译指示；用 volatile 阻止跨 fesetround 的
   常量折叠与重排，保证有向舍入真实作用于每次运算。 */
static double vload(double x) { volatile double t = x; return t; }

Ival iv_s(double x) { Ival r = {x, x}; return r; }
double iv_width(const Ival *a) { return a->hi - a->lo; }
int iv_finite(const Ival *a) { return isfinite(a->lo) && isfinite(a->hi); }

const char *iv_strerror(IvalStatus st) {
    switch (st) {
        case IVL_OK: return "ok";
        case IVL_TRUNC: return "interval truncated to zero width";
        case IVL_NONFINITE: return "non-finite interval endpoint";
        case IVL_DIVZERO: return "division by interval containing zero";
    }
    return "unknown";
}

/* 区间严格不跨零（端点同号且不含 0）：此时精确乘积/商必不为零，
   若被舍成零端点，就是真截断。 */
static int strict_nonz(const Ival *a) {
    return a->lo > 0.0 || a->hi < 0.0;
}

/* 统一收口：有向舍入后再向外各扩 1 ulp（硬件有向舍入对 10/64 这类
   结果两端可能相同，不扩 ulp 就会得到假零宽度区间）。任何有限值都
   存在外侧相邻浮点，所以扩 ulp 总能恢复有效包围；真正无法恢复的
   截断由乘/除在调用前按 FE_UNDERFLOW + 零端点判定。 */
static IvalStatus seal(Ival *r) {
    if (!isfinite(r->lo) || !isfinite(r->hi)) return IVL_NONFINITE;
    r->lo = nextafter(r->lo, -INFINITY);
    r->hi = nextafter(r->hi, INFINITY);
    return IVL_OK;
}

/* 下溢截断判定：异常置位、出现零端点、且两侧操作数都严格非零。 */
static IvalStatus underflowed(int raised, const Ival *r,
                              const Ival *a, const Ival *b) {
    if ((raised & FE_UNDERFLOW) && (r->lo == 0.0 || r->hi == 0.0) &&
        strict_nonz(a) && strict_nonz(b))
        return IVL_TRUNC;
    return IVL_OK;
}

IvalStatus iv_neg(const Ival *a, Ival *out) {
    out->lo = -a->hi;
    out->hi = -a->lo;
    return IVL_OK;
}

IvalStatus iv_add(const Ival *a, const Ival *b, Ival *out) {
    double al = vload(a->lo), ah = vload(a->hi);
    double bl = vload(b->lo), bh = vload(b->hi);
    fesetround(FE_DOWNWARD);
    double lo = al + bl;
    fesetround(FE_UPWARD);
    double hi = ah + bh;
    fesetround(FE_TONEAREST);
    out->lo = lo; out->hi = hi;
    return seal(out);
}

IvalStatus iv_sub(const Ival *a, const Ival *b, Ival *out) {
    double al = vload(a->lo), ah = vload(a->hi);
    double bl = vload(b->lo), bh = vload(b->hi);
    fesetround(FE_DOWNWARD);
    double lo = al - bh;
    fesetround(FE_UPWARD);
    double hi = ah - bl;
    fesetround(FE_TONEAREST);
    out->lo = lo; out->hi = hi;
    return seal(out);
}

IvalStatus iv_mul(const Ival *a, const Ival *b, Ival *out) {
    double al = vload(a->lo), ah = vload(a->hi);
    double bl = vload(b->lo), bh = vload(b->hi);
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_DOWNWARD);
    double d1 = al * bl, d2 = al * bh, d3 = ah * bl, d4 = ah * bh;
    double lo = fmin(fmin(d1, d2), fmin(d3, d4));
    fesetround(FE_UPWARD);
    double u1 = al * bl, u2 = al * bh, u3 = ah * bl, u4 = ah * bh;
    double hi = fmax(fmax(u1, u2), fmax(u3, u4));
    int raised = fetestexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
    out->lo = lo; out->hi = hi;
    IvalStatus st = underflowed(raised, out, a, b);
    if (st != IVL_OK) return st;
    return seal(out);
}

IvalStatus iv_div(const Ival *a, const Ival *b, Ival *out) {
    if (b->lo <= 0.0 && b->hi >= 0.0) return IVL_DIVZERO;
    double al = vload(a->lo), ah = vload(a->hi);
    double bl = vload(b->lo), bh = vload(b->hi);
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_DOWNWARD);
    double d1 = al / bl, d2 = al / bh, d3 = ah / bl, d4 = ah / bh;
    double lo = fmin(fmin(d1, d2), fmin(d3, d4));
    fesetround(FE_UPWARD);
    double u1 = al / bl, u2 = al / bh, u3 = ah / bl, u4 = ah / bh;
    double hi = fmax(fmax(u1, u2), fmax(u3, u4));
    int raised = fetestexcept(FE_ALL_EXCEPT);
    fesetround(FE_TONEAREST);
    out->lo = lo; out->hi = hi;
    IvalStatus st = underflowed(raised, out, a, b);
    if (st != IVL_OK) return st;
    return seal(out);
}

/* libm 三角不保证正确舍入：有向舍入求值后向外各扩 1 ulp 兜底。 */
static IvalStatus trig(double (*f)(double), const Ival *a, Ival *out) {
    double al = vload(a->lo), ah = vload(a->hi);
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(FE_DOWNWARD);
    double l1 = f(al), l2 = f(ah);
    double lo = fmin(l1, l2);
    fesetround(FE_UPWARD);
    double h1 = f(al), h2 = f(ah);
    double hi = fmax(h1, h2);
    fesetround(FE_TONEAREST);
    if (!isfinite(lo) || !isfinite(hi)) return IVL_NONFINITE;
    out->lo = lo; out->hi = hi;
    return seal(out);
}

IvalStatus iv_sin(const Ival *a, Ival *out) { return trig(sin, a, out); }
IvalStatus iv_cos(const Ival *a, Ival *out) { return trig(cos, a, out); }
