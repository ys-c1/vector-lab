#ifndef RIAUDIT_H
#define RIAUDIT_H
/*
 * 区间审计：把 pipeline.c 的仿射变换与 raster.c 的窗口映射逐阶段用区间算术
 * 重算，记录每步的上下界宽度与最坏偏差上界；另做经验性 ±1 ulp 扰动验证
 * 区间上界确实包住实际浮点偏差。
 *
 * 阶段顺序与浮点直算一一对应：
 *   IA_TRIG   旋转角的 cos/sin
 *   IA_AFFINE 顶点仿射 (p*c-p*y)*s+tx 等
 *   IA_WINDOW 窗口跨度与像素尺寸 dx/dy
 *   IA_SAMPLE 采样点坐标（确定性抽点，见 iaudit.c）
 *
 * 任一步出现 IVL_TRUNC / IVL_NONFINITE / IVL_DIVZERO 即拒绝整个场景，
 * 结果里带出失败阶段与原因。
 */
#include "scene.h"
#include "pipeline.h"
#include "interval.h"

typedef enum { IA_TRIG = 0, IA_AFFINE = 1, IA_WINDOW = 2, IA_SAMPLE = 3,
               IA_NSTAGES = 4 } IAStage;

typedef struct {
    IvalStatus st;
    double max_width;    /* 该阶段所有区间的最大宽度（世界单位） */
    double max_bound;    /* 该阶段最坏偏差上界，即区间宽度（世界单位） */
    double max_pixbd;    /* 换算到像素的最坏偏差上界 */
    double worst_dev;    /* 该阶段输出 ±1 ulp 经验散布（世界单位） */
    double worst_pix;    /* 经验偏差换算到像素 */
    int contained;       /* 经验偏差是否全部落在区间上界内 */
} IAStageResult;

typedef struct {
    IAStageResult stages[IA_NSTAGES];
    IAStage fail_stage;
    IvalStatus fail_status;
    int rejected;
    int over_tenth;      /* 上界超过画面一成（以像素上界判断） */
    double tenth_px;
    double elapsed_ms;
} IAReport;

int iaudit_run(const RScene *s, RPlace place, const RBox *win,
               int w, int h, IAReport *rep);
const char *ia_stage_name(IAStage st);
#endif
