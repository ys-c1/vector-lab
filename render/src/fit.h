#ifndef RFIT_H
#define RFIT_H
#include "pipeline.h"

/* 可辨认判定：点亮像素占比不低于满幅参考渲染的该比例，且至少 1 个点亮像素。 */
#define FIT_RECOG_DENSITY_RATIO 0.5
/* 允许搜索的最大边长，限制最坏情况下的耗时与内存。 */
#define FIT_MAX_CAP 4096

#define FIT_OK 0
#define FIT_BAD_ARG (-1)
#define FIT_BUDGET_BELOW_HEADER (-2) /* 预算连最小 PGM 头部都放不下 */
#define FIT_RENDER_FAIL (-3)
#define FIT_CAP_TOO_SMALL (-4)       /* 满幅参考本身就没有任何点亮像素 */
#define FIT_NO_FIT (-5)              /* 预算放不下任何带像素的尺寸 */
#define FIT_NOT_RECOGNIZABLE (-6)    /* 最大合规尺寸下图形已不可辨认 */

typedef struct {
    int status;        /* FIT_* */
    long long budget;  /* 传入的字节上限 */
    int cap;           /* 允许的最大边长 */
    int size;          /* 选中的方形边长；拒绝时为 0 */
    long long bytes;   /* 选中尺寸的实际 PGM 字节；头部不足时为最小头部字节 */
    int recognizable;  /* 选中尺寸是否仍可辨认 */
    int on;            /* 选中尺寸的点亮像素数 */
    int ref_on;        /* cap 满幅参考的点亮像素数 */
    int min_size;      /* 仍可辨认的最小边长；不存在时为 0 */
    int header_min;    /* 1x1 PGM 的头部字节数 */
} RFitResult;

/*
 * 在不超过 budget 字节的前提下，取不超过 cap 的最大方形边长。
 * place.scale 必须非 0，win 必须为有效窗口。结果写入 *out。
 * 返回值与 out->status 相同。
 */
int fit_solve(const RScene *s, RPlace place, RBox win, int cap,
              long long budget, RFitResult *out);

/* 状态码的可读说明，用于 CLI 与测试输出。 */
const char *fit_status_str(int status);
#endif
