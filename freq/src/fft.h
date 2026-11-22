#ifndef FQ_FFT_H
#define FQ_FFT_H

/* 迭代基 2 复数 FFT，二维按行列各做一遍。
 * 只在本模块内部使用；n 必须是 2 的幂。 */

typedef struct {
    int n;
    int *rev;
    double *cos_t;   /* cos(2*pi*k/n), k = 0..n/2-1 */
    double *sin_t;   /* sin(2*pi*k/n) */
} FqPlan;

int  fq_plan_init(FqPlan *p, int n);
void fq_plan_free(FqPlan *p);
void fq_fft_1d(const FqPlan *p, double *re, double *im, int stride, int inverse);
void fq_fft_2d(const FqPlan *p, double *re, double *im, int inverse);

#endif
