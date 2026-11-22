#ifndef RWINPLAN_H
#define RWINPLAN_H
#include "scene.h"
#include "pipeline.h"

/*
 * 贪心窗口规划：窗口大小固定，位置从候选格点里选；每次挑“覆盖未覆盖图形
 * 像素最多”的那块。覆盖率按图形像素（渲染后非零像素）统计，不是按整幅
 * 画面的像素统计——留白不算漏覆盖，图形像素没被任何窗口盖到才算。
 *
 * 候选格点：x 方向 0, stride, 2*stride, ... 直到 <= w-win_w，并补上贴右边
 * 界的 w-win_w；y 方向同理。窗口不越界。增益并列时取扫描序（先小 y 后
 * 小 x）的第一个，保证结果确定。
 *
 * 重叠率 = (窗口面积之和 - 窗口并集面积) / 窗口面积之和，按像素整数精确
 * 计算，取值 [0,1)。窗口大小相同且不越界，故 sum_area = nwins*win_w*win_h。
 *
 * 停止条件（每接受一个窗口后按次序判断，先命中者生效）：
 *   FULL    全部图形像素已被覆盖 —— 立即停，不再挑下一个窗口；
 *   TARGET  覆盖率 >= target；
 *   OVERLAP 所有剩余候选（有增益且未作废）都会让重叠率超上限，全被拒绝；
 *   NOGAIN  所有候选窗口对未覆盖像素的增益都是 0（目标未达即失败）；
 *   MAXWIN  窗口数达到 max_wins（目标未达即失败）；
 *   EMPTY   图形像素数为 0（空场景），覆盖率约定为 1，0 个窗口；
 *   FIXED   固定窗口数对照模式走完（不做贪心、不查重叠上限）。
 *
 * 重叠上限是硬约束：若当前增益最大的候选会超限，就拒绝它并记下“位置 /
 * 增益 / 若加上会达到的重叠率”，再试下一个候选；被拒的候选永久作废
 * （已接受集合只增不减，同一候选的重复评估只会得到更大的重叠率）。
 * 因此 OVERLAP 停止时的已接受窗口集合始终满足重叠上限。
 *
 * 关闭新逻辑（沿用原有 cli 的整幅单窗口 pipeline_render）时产物与改动前
 * 逐字节相同；winplan_render 在 stop==FULL 时也与它逐字节相同（同一
 * 采样相位，窗口外没有图形像素可画）。
 */

#define WINPLAN_MAX_WINS 256
#define WINPLAN_MAX_REJECTS 64     /* 详细记录的拒绝条数上限；超出只计数 */

enum {
    WINPLAN_STOP_FULL = 0,
    WINPLAN_STOP_TARGET = 1,
    WINPLAN_STOP_OVERLAP = 2,
    WINPLAN_STOP_NOGAIN = 3,
    WINPLAN_STOP_MAXWIN = 4,
    WINPLAN_STOP_FIXED = 5,
    WINPLAN_STOP_EMPTY = 6
};

enum {
    WINPLAN_OK = 0,
    WINPLAN_ERR_ARG = -1,      /* 参数非法 */
    WINPLAN_ERR_TARGET = -2,   /* 目标未达（NOGAIN / MAXWIN） */
    WINPLAN_ERR_OVERLAP = -3,  /* 重叠上限把所有剩余候选拒光 */
    WINPLAN_ERR_NOMEM = -4
};

typedef struct { int x, y, w, h; } RWin;
typedef struct { int x, y, gain, after_wins; double overlap; } RWinReject;
typedef struct {
    int win_w, win_h;    /* 窗口大小（像素），必须 <= 画面尺寸 */
    int stride;          /* 候选格点步长（像素），>= 1；可大于窗口（会留缝） */
    double target;       /* 覆盖率目标，(0, 1] */
    double overlap_cap;  /* 重叠率上限，[0, 1) */
    int max_wins;        /* 窗口数上限，1..WINPLAN_MAX_WINS */
    int fixed_count;     /* >0：固定窗口数对照模式，按扫描序取前 N 个 */
} RWinPlanOpt;

typedef struct {
    RWin wins[WINPLAN_MAX_WINS];
    int gain[WINPLAN_MAX_WINS];   /* 该窗口新增覆盖的图形像素数 */
    int nwins;
    RWinReject rejects[WINPLAN_MAX_REJECTS];
    int nrejects;                 /* 被重叠上限拒绝的候选窗口总数 */
    int on_pixels;                /* 图形像素总数 */
    int covered;                  /* 被窗口覆盖的图形像素数 */
    double coverage;              /* covered / on_pixels；空场景约定为 1 */
    long long sum_area;           /* nwins * win_w * win_h */
    long long union_area;         /* 窗口并集像素数 */
    double overlap;               /* (sum_area - union_area) / sum_area */
    int stop;                     /* WINPLAN_STOP_* */
    double plan_ms;
} RWinPlan;

/* 默认参数：40x40、步长 8、目标 0.98、重叠上限 0.60、最多 256 个窗口。 */
int winplan_default_opt(RWinPlanOpt *o);

/* 候选格点数；参数非法（含窗口超画面）返回 -1。 */
int winplan_candidate_count(int w, int h, const RWinPlanOpt *o);

/* on 为 w*h 的图形像素掩码（非零即图形）。0 成功；参数错 -1，内存不足 -4，
 * 目标未达 -2，重叠上限拒光 -3。出错时 out 里仍保留已接受窗口与统计。 */
int winplan_plan(const unsigned char *on, int w, int h,
                 const RWinPlanOpt *o, RWinPlan *out);

/* 按计划里的窗口渲染：变换场景一次，再逐窗口用与 raster_fill 相同的采样
 * 相位（ss=4）只填窗口像素，窗口外保持 0。返回 0 或 pipeline/raster 错误码。 */
int winplan_render(const RScene *s, RPlace place, RBox win,
                   const RWinPlan *p, int w, int h, unsigned char *buf);

const char *winplan_stop_str(int stop);

#endif
