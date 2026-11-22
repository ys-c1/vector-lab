#include "quant.h"
#include <math.h>

/* ---- 步长反解的数学常量（见 README 推导） ----
 * Δ_b = clamp( Δ0 * ( Emax / (E_b + ε) )^γ , 1 .. Q_CLAMP_MUL·Δ0 )
 * γ 控制步长对能量差异的敏感度（0<γ≤1，保持“高能小步长”单调）；
 * ε 仅用于数值稳定，常数带走无损直通，不参与反解。 */
#ifndef Q_GAMMA
#define Q_GAMMA 0.35
#endif
#ifndef Q_CLAMP_MUL
#define Q_CLAMP_MUL 8.0
#endif
#ifndef Q_BASE_HIGH
#define Q_BASE_HIGH 1.0
#endif
#ifndef Q_BASE_BALANCED
#define Q_BASE_BALANCED 1.5
#endif
#ifndef Q_BASE_SMALL
#define Q_BASE_SMALL 2.0
#endif
#ifndef Q_BASE_TINY
#define Q_BASE_TINY 3.0
#endif
#define Q_EPS 1.0

int quant_preset_base(QPreset q) {
    switch (q) {
        case Q_HIGH:     return (int)(Q_BASE_HIGH     * 65536.0);
        case Q_BALANCED: return (int)(Q_BASE_BALANCED * 65536.0);
        case Q_SMALL:    return (int)(Q_BASE_SMALL    * 65536.0);
        case Q_TINY:     return (int)(Q_BASE_TINY     * 65536.0);
    }
    return -1;
}

const char *quant_preset_name(QPreset q) {
    static const char *names[4] = {"high", "balanced", "small", "tiny"};
    if ((int)q < 0 || (int)q > 3) return "?";
    return names[q];
}

/* 对称舍入：round-half-away-from-zero */
static int64_t round_div(int64_t num, int64_t den) {
    if (num >= 0) return (num + den / 2) / den;
    return -((-num + den / 2) / den);
}

int quant_analyze(const int32_t *a, int w, const WBand *bands, int nb,
                  uint32_t base_q16, QBandStat *out) {
    if (!a || !bands || nb <= 0 || !out) return -1;
    if (base_q16 == 0u) return -3;                 /* 基准步长必须为正 */

    double emax = 0.0;
    for (int b = 0; b < nb; ++b) {
        const WBand *bd = &bands[b];
        int64_t n = (int64_t)bd->w * bd->h, sum = 0;
        for (int y = 0; y < bd->h; ++y) {
            const int32_t *p = a + (bd->y + y) * w + bd->x;
            for (int x = 0; x < bd->w; ++x) sum += p[x];
        }
        int32_t mean = (int32_t)round_div(sum, n);
        int64_t acc = 0;
        for (int y = 0; y < bd->h; ++y) {
            const int32_t *p = a + (bd->y + y) * w + bd->x;
            for (int x = 0; x < bd->w; ++x) {
                int64_t d = (int64_t)p[x] - mean;
                acc += d * d;
            }
        }
        double e = (double)acc / (double)n;
        out[b].energy = e;
        out[b].mean = mean;
        out[b].constant = (acc == 0);
        if (e > emax) emax = e;
    }

    /* 整幅图为纯色（或平坦）时所有带均为常数，全部无损直通。
     * 最粗 LL（bands[0]）始终无损保护：它系数极少却主导重建质量。 */
    for (int b = 0; b < nb; ++b) {
        if (out[b].constant || b == 0) {
            out[b].step_q16 = 1u << 16;
            continue;
        }
        /* 反解：能量越高步长越小，能量越低步长越大（仅作用于高频带） */
        double d = (double)base_q16 *
                   pow(emax / (out[b].energy + Q_EPS), Q_GAMMA);
        if (!(d > 0.0) || !isfinite(d)) return -2;   /* 零或负/非有限值一律拒绝 */
        double hi = Q_CLAMP_MUL * (double)base_q16;  /* 相对上限，随档位收紧 */
        if (d > hi) d = hi;
        if (d < 1.0) d = 1.0;
        out[b].step_q16 = (uint32_t)d;
        if (out[b].step_q16 == 0u) return -2;
    }
    return 0;
}

int32_t quant_enc(int32_t resid, uint32_t step_q16) {
    int64_t num = (int64_t)resid << 16;
    return (int32_t)round_div(num, (int64_t)step_q16);
}

int32_t quant_dec(int32_t code, uint32_t step_q16) {
    int64_t v = (int64_t)code * (int64_t)step_q16;
    return (int32_t)round_div(v, (int64_t)1 << 16);
}

int quant_check_steps(const QBandStat *st, int nb) {
    if (!st || nb <= 0) return -1;
    for (int b = 0; b < nb; ++b)
        if (st[b].step_q16 == 0u) return -2;
    /* uint32_t 无负值可能；解码读入时对有符号字段另行拒绝负值 */
    return 0;
}
