#ifndef RGAMMA_H
#define RGAMMA_H

#include <stddef.h>

/*
 * 灰度伽马反解（gamma inversion）。
 *
 * 编码模型（与“线性导出”相对的一条可选通路）：
 *
 *     y = round( 255 * (x / 255) ^ (1 / gamma) ),   x, y 为 0..255 的灰度
 *
 * 线性导出就是 gamma = 1（逐字节恒等，见 export_pgm）。
 *
 * 反解：已知一张参照图 ref 和一张线性灰度源图 src，按两者灰度直方图做
 * 非线性最小二乘，解出使残差最小的 gamma：
 *
 *     residual(g) = sqrt( 1/256 * sum_{v=0}^{255}
 *                         ( CDF_src_g[v] - CDF_ref[v] )^2 )
 *
 * 其中 CDF_src_g 是 src 的直方图经 g 映射后的累积分布，CDF_ref 是参照图的
 * 累积分布。残差是 [0,1] 上的无量纲量（两条 CDF 的 RMS 距离），阈值为
 * 1e-3（千分之一）。
 *
 * 拟合方法（不依赖导数，单调问题全局扫描）：
 *   1. 在 log(gamma) 上取 GAM_COARSE 段粗网格扫描（gamma 范围
 *      [GAM_GAMMA_MIN, GAM_GAMMA_MAX]）；
 *   2. 在最好点两侧各一格内做 GAM_ZOOM_ROUNDS 轮逐级加密（每轮 GAM_ZOOM
 *      个点），直到残差为 0 或网格宽度收敛；
 *   3. 若残差为 0，说明存在一段“零残差平台”（该区间内每个 gamma 都把源
 *      直方图逐级映射成与参照完全相同的直方图）。在 log(gamma) 上向两侧
 *      倍增探测边界，再二分到相对宽度 < 1e-12，取平台几何中心为解。
 *
 * 为什么平台中心的重编码与参照逐字节相同：
 *   residual(g)=0 等价于映射后的源直方图与参照直方图逐级相等；又因
 *   x -> gamma_encode_level(x,g) 关于 x 单调不减，参照的编码函数同样单调，
 *   两个单调序列若有相同的多重集则逐项相等，于是逐像素映射结果与参照一致。
 *   平台边界由量化舍入的临界穿越决定，中心离两侧边界最远，数值最稳。
 *
 * 前置拒绝：
 *   - gamma 参数为 0、负数或非有限（NaN/Inf）：返回 GAM_E_GAMMA，
 *     err_field / field 写明 "gamma"。
 *   - 全黑或全白（源或参照只有一个灰度级）无法拟合：返回 GAM_E_FLAT，
 *     out->flat=1，out->flat_level=该灰度级，err_field 写 "src" 或 "ref"。
 *
 * 已知参照与源必须同尺寸；尺寸不符返回 GAM_E_SIZE（field="ref"）。
 */

#define GAM_FIELD_MAX 32

/* 拟合搜索范围（只约束反解；显式传入的 gamma 只要为正有限即可编码）。 */
#define GAM_GAMMA_MIN 0.05
#define GAM_GAMMA_MAX 20.0

#define GAM_COARSE 256        /* 首轮 log 网格段数 */
#define GAM_ZOOM 512          /* 每轮加密点数 */
#define GAM_ZOOM_ROUNDS 6     /* 加密轮数 */

enum {
    GAM_OK = 0,
    GAM_E_ARG = -1,    /* 空指针 / 非正尺寸：field = "src"/"ref"/"buf"/"w"/"h"/"out" */
    GAM_E_GAMMA = -2,  /* gamma <= 0 或非有限：field = "gamma" */
    GAM_E_SIZE = -3,   /* 参照与源尺寸不同：field = "ref" */
    GAM_E_FLAT = -4,   /* 单一灰度级，无法拟合：field = "src"/"ref" */
    GAM_E_IO = -5,     /* PGM 打不开：field = "path" */
    GAM_E_PGM = -6     /* PGM 格式错误 / 数据截断：field = "path" */
};

typedef struct {
    double gamma;      /* 解出的 gamma（GAM_OK 时有效） */
    double residual;   /* 该 gamma 处的 CDF RMS 残差 */
    double lo, hi;     /* 零残差平台两端（residual==0 时）；否则 lo==hi==gamma */
    int evals;         /* 残差函数求值次数 */
    double ms;         /* 拟合耗时（毫秒） */
    int flat;          /* 1 = 无法拟合（单一灰度级） */
    int flat_level;    /* flat 时唯一的灰度级（0=全黑，255=全白） */
    int at_lo, at_hi;  /* 平台/最优点顶到搜索边界 */
    char err_field[GAM_FIELD_MAX]; /* 出错或 flat 的字段名 */
} GammaFit;

/* 校验显式 gamma 参数。成功返回 GAM_OK；失败返回 GAM_E_GAMMA 并把
 * field 写成 "gamma"（field 可为 NULL）。 */
int gamma_validate(double g, char *field, size_t cap);

/* 单级灰度编码 y = round(255*(x/255)^(1/g))。
 * 调用方应先用 gamma_validate 校验 g；0/255 恒等，1..254 做幂映射。 */
unsigned char gamma_encode_level(int level, double g);

/* 对整块像素做编码。gamma 非法时返回 GAM_E_GAMMA，field="gamma"；
 * 指针/尺寸非法返回 GAM_E_ARG。允许 in == out（原地）。 */
int gamma_apply(const unsigned char *in, int w, int h, double g,
                unsigned char *out, char *field, size_t cap);

/* 统计 256 级直方图。 */
void gamma_hist(const unsigned char *buf, int w, int h, size_t hist[256]);

/* 给定源/参照直方图，返回 gamma 处的 CDF RMS 残差（不校验 gamma）。 */
double gamma_residual_hist(const size_t *src, size_t sn,
                           const size_t *ref, size_t rn, double g);

/* 按直方图反解 gamma。src/ref 为 256 项计数，sn/rn 为各自像素总数。 */
int gamma_fit_hist(const size_t *src, size_t sn,
                   const size_t *ref, size_t rn, GammaFit *out);

/* 按像素反解 gamma（src 与 ref 必须同尺寸）。 */
int gamma_fit(const unsigned char *src, const unsigned char *ref,
              int w, int h, GammaFit *out);

/* 读取 P5 / maxval 255 的 PGM；成功时 *buf 由 malloc 分配，调用方 free。
 * 失败返回 GAM_E_IO（打不开）或 GAM_E_PGM（格式/尺寸/截断），field="path"。 */
int gamma_load_pgm(const char *path, unsigned char **buf, int *w, int *h,
                   char *field, size_t cap);

#endif
