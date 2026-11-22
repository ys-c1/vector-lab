#ifndef RPIPE_H
#define RPIPE_H
#include "scene.h"
typedef struct { double tx, ty, rot, scale; } RPlace;

/* 整数层旧路径：直接按 place.scale 变换后二值填充，
 * 与引入缩放层之前逐字节一致。 */
int pipeline_render(const RScene *s, RPlace place, RBox win, int w, int h, unsigned char *buf);

/* 连续层路径：把 log2(scale) 拆成整数层与过渡权重。
 * blend=1 时两档各渲染覆盖率再按权重插值；
 * blend=0 时退回整数层的二值填充（硬切），整数层上输出与旧路径逐字节相同。 */
typedef struct {
    int level; /* 指定层号，-1 表示由 place.scale 反解；给出时按该层号取缩放 */
    int ss;    /* 超采样数 1..8，<=0 用默认 4 */
    int blend; /* 1 过渡，0 硬切 */
} RZoomOpts;

typedef struct {
    double level;              /* 实际使用的层号 */
    int lo, hi;                /* 两档整数层（exact 时只用 lo） */
    double w;                  /* 过渡权重：取 hi 档的比例 */
    int exact;                 /* 层号恰为整数 */
    double scale_lo, scale_hi; /* 两档各自的缩放 2^lo、2^hi */
    double scale_eff;          /* 层号对应的缩放 2^level */
    unsigned long us;          /* 本帧渲染耗时，微秒 */
} RZoomReport;

/* 返回：0 成功；-1 参数；-2 scale 为零或负；-3 窗口非法；
 * -4 场景容量不足；-5 层号非法；-6 内存；-7 scale 非有限。 */
int pipeline_render_zoom(const RScene *s, RPlace place, RBox win, int w, int h,
                         const RZoomOpts *opt, unsigned char *buf, RZoomReport *rep);
#endif
