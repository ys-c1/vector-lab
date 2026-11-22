#ifndef RPARAMSOLVE_H
#define RPARAMSOLVE_H
#include <stddef.h>
#include "scene.h"
#include "pipeline.h"

/*
 * 二维参数反解：把“超采样数 ss”和“位深 bits”当成二维参数网格，
 * 搜索哪种 (ss, bits) 组合导出的 PGM 与参照图逐字节一致。
 *
 * 渲染模型（与 gold/ref.c 的覆盖率口径对齐）：
 *   每个像素取 ss x ss 个子样本（与 raster_fill 完全相同的格点与
 *   射线奇偶判定），得到覆盖率 c ∈ [0, ss^2]。
 *
 *   bits == 1（旧路径口径）：
 *     c > 0 -> 255，否则 0。等价于 raster_fill 的 any-hit 二值化，
 *     因此 (ss=4, bits=1) 与本仓库原来的 pipeline_render 输出逐字节相同。
 *   bits ∈ [2, 8]：
 *     L = 2^bits - 1 个量化台阶，level = floor(c * L / ss^2)，
 *     灰度 = round(255 * level / L)。
 *     bits=8、ss=8 时 level = floor(255*c/64)、灰度 = level，
 *     与 gold 的 (unsigned char)(255*c/(SS*SS)) 逐字节一致。
 *
 * 合法域：ss ∈ [1, 8]，bits ∈ [1, 8]。越界一律拒绝并写明字段名。
 */

#define PS_SS_MIN   1
#define PS_SS_MAX   8
#define PS_BITS_MIN 1
#define PS_BITS_MAX 8
#define PS_FIELD_MAX 16
#define PS_MAX_MATCHES (PS_SS_MAX * PS_BITS_MAX)

enum {
    PS_OK = 0,
    PS_E_ARG = -1,     /* 空指针 / 非正尺寸 / out 为空 */
    PS_E_SS = -2,      /* ss 越界：field = "ss" */
    PS_E_BITS = -3,    /* bits 越界：field = "bits" */
    PS_E_RANGE = -9,   /* 区间倒置（lo > hi）：field = "ss"/"bits" */
    PS_E_SIZE = -4,    /* 参照尺寸与 w,h 不符：field = "ref" */
    PS_E_NOMATCH = -5, /* 合法域内没有任何组合能逐字节复现参照 */
    PS_E_PGM = -6,     /* PGM 格式错误 / 截断 / maxval != 255：field = "path" */
    PS_E_IO = -7,      /* PGM 打不开：field = "path" */
    PS_E_MEM = -8      /* 分配失败 */
};

typedef struct {
    int ss, bits;
    long diff_bytes;   /* 与参照不相同的字节数 */
    double render_ms;  /* 该组合的渲染+量化耗时 */
} PSCombo;

typedef struct {
    int ss, bits;      /* 选中的组合（PS_OK 时） */
    long diff_bytes;   /* 选中组合的差异字节数，PS_OK 时为 0 */
    int nmatch;        /* 逐字节一致的组合个数 */
    PSCombo matches[PS_MAX_MATCHES];
    int chosen;        /* 选中项在 matches 中的下标 */
    int ambiguous;     /* nmatch > 1 */
    int prefer_min;    /* 1=并列时取 (ss,bits) 最小；0=取最大 */
    double total_ms;   /* 整个搜索耗时 */
    double coverage_ms[PS_SS_MAX + 1]; /* 各 ss 覆盖率渲染耗时；未用为 -1 */
} PSResult;

/* 校验单个参数：合法返回 PS_OK；越界返回 PS_E_SS / PS_E_BITS，
 * 并把 field 写成 "ss" / "bits"（field 可为 NULL / cap=0）。 */
int ps_validate_ss(int ss, char *field, size_t cap);
int ps_validate_bits(int bits, char *field, size_t cap);

/* 子样本覆盖率量化为灰度。要求 1<=ss<=8、1<=bits<=8。 */
unsigned char ps_quantize(int cov, int ss, int bits);

/* 按 (ss, bits) 渲染场景到 buf（长度 w*h）。与 pipeline_render 同样先做
 * 仿射放置 move()，再在 win 上栅格化；place.scale==0 返回 PS_E_ARG。 */
int ps_render(const RScene *s, RPlace place, RBox win,
              int w, int h, int ss, int bits, unsigned char *buf);

/* 在 ss/bits 子区间内穷举，找出与 ref 逐字节一致的所有组合。
 * ss_lo..ss_hi 与 bits_lo..bits_hi 必须落在合法域内且 lo<=hi，否则分别
 * 返回 PS_E_SS / PS_E_BITS。prefer_min 决定并列时的选取方向。
 * ref 必须恰为 w*h 字节。无匹配返回 PS_E_NOMATCH（out->nmatch=0）。 */
int ps_solve(const RScene *s, RPlace place, RBox win, int w, int h,
             const unsigned char *ref,
             int ss_lo, int ss_hi, int bits_lo, int bits_hi,
             int prefer_min, PSResult *out);

/* 计数字节差异。 */
long ps_diff(const unsigned char *a, const unsigned char *b, int n);

/* 读取 P5 / maxval 255 PGM；成功时 *buf 由 malloc 分配，调用方 free。
 * 失败返回 PS_E_IO（打不开）或 PS_E_PGM（格式/尺寸/截断），field="path"。 */
int ps_load_pgm(const char *path, unsigned char **buf, int *w, int *h,
                char *field, size_t cap);

const char *ps_strerror(int code);

#endif
