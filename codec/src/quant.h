#ifndef CODEC_QUANT_H
#define CODEC_QUANT_H
#include <stdint.h>
#include "wavelet.h"

/* 四个质量档：base 为最粗 LL 的基准步长（Q16 定点，1<<16 表示 1.0）。 */
typedef enum { Q_HIGH = 0, Q_BALANCED = 1, Q_SMALL = 2, Q_TINY = 3 } QPreset;
int quant_preset_base(QPreset q);          /* 返回 Q16 base */
const char *quant_preset_name(QPreset q);

/* 单个子带的统计与量化参数。 */
typedef struct {
    double energy;     /* AC 能量 E = Var（常数带为 0） */
    int32_t mean;      /* 整数均值（四舍五入），量化在零均值残差上进行 */
    int constant;      /* 该带所有系数相同 => 仅存均值，无损直通 */
    uint32_t step_q16; /* 反解得到的量化步长（Q16），恒 > 0 */
} QBandStat;

/* 对所有子带做统计，并按能量反解量化步长。
 * base_q16 必须 > 0；任何反解结果 <= 0 时返回 -2（拒绝）。 */
int quant_analyze(const int32_t *a, int w, const WBand *bands, int nb,
                  uint32_t base_q16, QBandStat *out);

/* 量化 / 反量化（在零均值残差上做对称舍入量化）。 */
int32_t quant_enc(int32_t resid, uint32_t step_q16);
int32_t quant_dec(int32_t code, uint32_t step_q16);

/* 编码侧校验：任一子带步长为零或负则拒绝（返回非 0）。 */
int quant_check_steps(const QBandStat *st, int nb);
#endif
