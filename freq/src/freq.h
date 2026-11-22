#ifndef FREQ_H
#define FREQ_H

/*
 * 频域定截止滤波。
 *
 * 问题：细密纹理用空域低通（盒式/高斯）怎么调都会糊，因为固定的空域核
 * 无法区分“纹理基频”和“噪声高频”。这里改成频域：
 *
 *   1. 去均值后做二维 FFT（反射填充到 2 的幂，图像居中）；
 *   2. 按半径分环带统计平均功率，用 AC 环带中位数作噪声底，
 *      以 max(noise_mult × 底噪, 1e-8 × 峰值) 作显著门限；
 *   3. 截止频率取“最外显著环带的右边界”，cycles/pixel；
 *   4. 用 1 环带宽的升余弦过渡做低通，逆变换回空域；
 *   5. 逆变换后实测 AC 方差比，与频域预测的保留比例对照（反变换验证）。
 *
 * 拒绝规则：
 *   - 没有显著环带（截止频率为零）           -> FQ_E_CUTOFF_ZERO
 *   - 截止频率超过奈奎斯特对角线（含角点频率） -> FQ_E_CUTOFF_HIGH
 * 纯色图（所有像素相同）没有 AC 能量，单独标为 FQ_ST_FLAT，不做截止。
 *
 * 兼容路径：use_freq == 0（或编译期 FQ_ENABLE_FREQ=0）时完全不做频域分析，
 * 只走原有的空域高斯低通 fq_filter_spatial()。该路径的输出锁定在
 * baseline/ 里逐字节比对，新增频域逻辑不得改动它。
 */

#ifndef FQ_ENABLE_FREQ
#define FQ_ENABLE_FREQ 1
#endif

/* 奈奎斯特对角线，cycles/pixel */
#define FQ_CUTOFF_MAX 0.70710678118654752
/* 空域兼容路径的高斯核半径上限（固定点实现） */
#define FQ_SPATIAL_MAX_R 8
/* FFT 边长上限（像素） */
#define FQ_MAX_FFT 1024

/* 返回码 */
enum {
    FQ_OK = 0,
    FQ_E_ARG = -1,          /* 空指针 */
    FQ_E_DIM = -2,          /* 宽高越界（需 2..FQ_MAX_FFT） */
    FQ_E_MEM = -3,          /* 内存不足 */
    FQ_E_CUTOFF_ZERO = -4,  /* 截止频率为零，拒绝 */
    FQ_E_CUTOFF_HIGH = -5,  /* 截止频率超过上限，拒绝 */
    FQ_E_DISABLED = -6      /* 编译期关闭频域逻辑 */
};

/* 报告状态 */
enum {
    FQ_ST_OK = 0,
    FQ_ST_FLAT = 1,        /* 纯色图：单独报出 */
    FQ_ST_REJ_ZERO = 2,    /* 截止频率为零，已拒绝 */
    FQ_ST_REJ_HIGH = 3     /* 截止频率超上限，已拒绝 */
};

typedef struct {
    int use_freq;        /* 1 = 频域定截止（新逻辑）；0 = 原有空域滤波 */
    double sigma;        /* 空域高斯标准差（像素）；<=0 时为空操作 */
    double noise_mult;   /* 噪声底倍数，默认 8 */
    double cutoff_max;   /* 截止上限，默认 FQ_CUTOFF_MAX；超过按上限计 */
} FqOptions;

typedef struct {
    int status;             /* FQ_ST_* */
    int flat;               /* 纯色图标记 */
    int nfft;               /* FFT 边长 N（0 表示未做频域分析） */
    double cutoff;          /* 选定截止频率，cycles/pixel */
    double retained;        /* 频域预测保留的 AC 能量比例 */
    double retained_meas;   /* 逆变换后实测 AC 能量比例（验证用） */
    int sig_bins;           /* 显著的环带数 */
    double noise_floor;     /* AC 环带中位数（噪声底） */
    double peak;            /* AC 环带均值峰值 */
    double threshold;       /* 实际显著门限 */
    double roundtrip_err;   /* 无滤波往返最大绝对误差 */
    double ms_prep;
    double ms_pad;
    double ms_forward;
    double ms_verify;
    double ms_select;
    double ms_inverse;
    double ms_assemble;
    double ms_total;
    char reason[192];       /* 拒绝/纯色说明 */
} FqReport;

void fq_options_default(FqOptions *o);

/* 完整流程。返回 FQ_OK（含纯色）或 FQ_E_*。
 * 拒绝时 out 仍被填成输入副本，便于上层落盘对照。 */
int fq_run(const unsigned char *in, int w, int h, const FqOptions *opt,
           unsigned char *out, FqReport *rep);

/* 只做频域分析并选截止，不输出图像（out 可为 NULL）。 */
int fq_choose_cutoff(const unsigned char *in, int w, int h,
                     const FqOptions *opt, FqReport *rep);

/* 原有空域低通：可分离高斯，固定点权（Q16），反射边界。
 * sigma <= 1e-6 时为逐字节空操作。 */
int fq_filter_spatial(const unsigned char *in, int w, int h, double sigma,
                      unsigned char *out);

/* 给定截止频率的频域低通；同时返回预测/实测保留比例。 */
int fq_filter_freq(const unsigned char *in, int w, int h, double cutoff,
                   unsigned char *out, double *retained, double *retained_meas);

/* 无滤波往返：max |ifft(fft(x)) - x|，用于验证变换正确性。 */
double fq_roundtrip_err(const unsigned char *in, int w, int h);

/* 模糊度：1 - Re<F_out, F_ref> / |F_ref|^2（只计 AC 环带）。
 * 0 = 与参考完全一致；越大越糊。两侧用同一把尺子，与滤波器无关。 */
double fq_blur_degree(const unsigned char *out, const unsigned char *ref,
                      int w, int h);

/* 截止之上的高频能量被去除的比例：1 - E_out(>fc) / E_in(>fc)。 */
double fq_hf_removed(const unsigned char *out, const unsigned char *in,
                     int w, int h, double cutoff);

const char *fq_strerror(int code);

#endif
