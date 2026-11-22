#ifndef SWEEP_H
#define SWEEP_H

#include <stdio.h>

/*
 * 扫描线候选对：逐事件处理 vs 合并窗口。
 *
 * 现状（legacy，逐事件处理）：
 *   每个线段在扫描方向（x）上展开成 [lo, hi]（lo=min(x0,x1)）。
 *   把 2n 个事件（进入 lo、离开 hi）按 (时间, 进入优先, 段号) 排序，
 *   每处理完一个事件就枚举当前活动段的所有段对、做一次相交测试。
 *   同一时刻有 k 个事件时就要枚举 k 次活动集，候选对因此被重复放大；
 *   一个段在其生命期内每经过一个事件也会被重新枚举一遍。
 *   这就是“同一时刻的事件拆得太散，候选对涨上去”的来源。
 *
 * 新逻辑（合并窗口）：
 *   把时间轴上相邻的不同事件时刻按间隔 <= W 链式合并成一组（时间差分布
 *   决定分组边界）。每组取活动段数最多的时刻 t* 作为代表时刻，只枚举
 *   t* 的活动段对；超过一个事件时刻的窗口会漏掉“只在窗口内部短暂共存”
 *   的段对，因此可开启审计：把窗口内并集 U 中、不在 t* 活动集里的段对
 *   逐对做真实相交测试，一旦发现相交就拒绝该 W 并给出来龙去脉
 *   （sweep_format_reject）。
 *
 *   事件时间差为零的段（lo == hi，扫描方向上的零长段）不参与窗口代表
 *   时刻的选择与快照，而是在其唯一时刻单独成桶，和该时刻的全体活动段
 *   配对；这些段与它们贡献的候选对、交点数在统计里单独列出。
 *
 * 反解窗口：
 *   sweep_gap_dist 给出事件时间差分布（不同正间隔及其出现次数、零间隔数），
 *   sweep_choose 以 {0} ∪ 分布中的正间隔为候选 W，逐档用
 *       est = Σ_组 C(代表时刻非零活动段数, 2)
 *             + Σ_零时刻 (C(该时刻活动段数,2) - C(活动段数-零长段数,2))
 *   估出合并后的候选对数（该式只用到时间差分布与各时刻活动计数，不枚举
 *   段对），再实际枚举/审计。只在审计安全的窗口里选总代价
 *   （候选对 + 审计对）最小者，并要求候选对至少下降 target（如 0.20）。
 *
 * 关闭新逻辑（flags 不含 SW_F_MERGE）时走 legacy 逐事件路径；与独立参照
 * 实现（compat.c）的交点表逐字节相同。
 */

typedef struct { double x0, y0, x1, y1; } SwSeg;

/* 交点记录。snap：legacy 为事件序号，合并模式为组号；ia<ib。 */
typedef struct {
    int snap;
    int ia, ib;
    double t, u;   /* 在 ia / ib 上的参数 [0,1] */
    double x, y;   /* 交点坐标 */
} SwHit;

typedef struct { double gap; int count; } SwGap; /* 正时间差及其次数 */

/* 返回码 */
enum {
    SW_OK = 0,
    SW_E_ARG = -1,     /* 空指针 / n 越界 */
    SW_E_SEG = -2,     /* 端点坐标非有限 */
    SW_E_WINDOW = -3,  /* 窗口为负或非有限 */
    SW_E_FLAGS = -4,   /* 未知标志位 */
    SW_E_CAP = -5,     /* 交点表容量不足 */
    SW_E_IO = -6,      /* CSV 写出失败 */
    SW_E_NOMEM = -7    /* 内存不足 */
};

#define SW_F_MERGE 1u  /* 打开合并窗口（新逻辑）；不带则逐事件处理 */
#define SW_F_AUDIT 2u  /* 合并模式：审计窗口内风险段对，发现漏交即拒绝 */

/* 窗口拒绝 / 安全性说明 */
enum {
    SW_SAFE_UNKNOWN = -1, /* 未开启审计 */
    SW_SAFE = 1,
    SW_UNSAFE = 0
};
enum {
    SW_REJ_NONE = 0,
    SW_REJ_MISS = 1,   /* 窗口内存在只在窗口内部共存的相交段对 */
    SW_REJ_ARG = 2
};

typedef struct {
    double window;          /* 合并窗口宽度 */
    int merge;              /* 1 = 合并模式 */
    int audited;            /* 1 = 做过安全审计 */

    int events;             /* 事件数 2n */
    int times;              /* 不同事件时刻数 */
    int zero_gap_events;    /* 排序后相邻事件时间差为 0 的次数 = events - times */
    int zero_segs;          /* 扫描方向零长段数（lo == hi，事件时间差为零） */
    int zero_buckets;       /* 含零长段的时刻桶数 */

    int groups;             /* 合并组数；legacy 时为 times */

    long long cand_legacy;  /* 逐事件处理的候选对枚举数 */
    long long cand_merged;  /* 本窗口的候选对枚举数（含零长段桶） */
    long long cand_zero;    /* 其中涉及零长段的候选对（单独统计） */
    long long cand_est;     /* 由时间差分布 + 活动计数估出的候选对数 */
    long long audit_pairs;  /* 审计实际测试的风险段对数 */

    int safe;               /* SW_SAFE / SW_UNSAFE / SW_SAFE_UNKNOWN */
    int target_met;         /* sweep_choose：是否达到候选对下降目标 */
    int reject_reason;      /* SW_REJ_* */

    /* 第一处漏交（拒绝原因；仅 SW_UNSAFE 时有效） */
    int miss_ia, miss_ib;
    double miss_t0, miss_t1; /* 出问题的合并窗口 [t0, t1] */
    double miss_t;           /* 代表时刻 t* */
    double miss_x, miss_y;   /* 漏掉的交点 */

    int hits;               /* 写出/统计到的交点记录数（legacy 可含重复） */
    int unique_hits;        /* 去重后的交点段对数 */
    int hits_zero;          /* 涉及零长段的交点数（去重后） */
} SwStats;

/* 旧逻辑：逐事件处理。out 可为 NULL（只统计，cap 忽略）。 */
int sweep_legacy(const SwSeg *segs, int n, SwHit *out, int cap,
                 int *nhits, SwStats *st);

/*
 * 统一入口。
 *   flags 不含 SW_F_MERGE：忽略 W，走逐事件路径（与 sweep_legacy 逐字节一致）。
 *   flags 含 SW_F_MERGE：按窗口 W 合并；W 必须有限且 >= 0。
 *     SW_F_AUDIT 打开安全审计（开销记入 audit_pairs）。
 * out 可为 NULL；cap 不足返回 SW_E_CAP（已写出的记录保持）。
 */
int sweep_run(const SwSeg *segs, int n, double W, unsigned flags,
              SwHit *out, int cap, int *nhits, SwStats *st);

/* 事件时间差分布：不同正间隔升序写入 out（最多 cap 条），零间隔单独计数。 */
int sweep_gap_dist(const SwSeg *segs, int n, SwGap *out, int cap,
                   int *nout, int *zero_gap_events, int *zero_span_segs);

/*
 * 只靠“事件时间差分布 + 各时刻活动计数”估算窗口 W 下的候选对数，不枚举
 * 任何段对、不做相交测试：
 *   est = Σ_组 C(代表时刻非零活动段数, 2)
 *       + Σ_零长段时刻 (C(该时刻活动段数,2) - C(非零活动段数,2))
 * 代表时刻取组内活动段数最多（平局取最早）的事件时刻；分组规则与
 * sweep_run 的合并模式完全相同。结果与 sweep_run(SW_F_MERGE) 的
 * cand_merged 逐值相等（见 test.c 与 demo 的“估计/实测”一栏）。
 */
int sweep_estimate_pairs(const SwSeg *segs, int n, double W, long long *est);

/*
 * 反解最优窗口：候选 W = {0} ∪ 分布中的正间隔（升序）。
 * 每档实际枚举（并审计），只在安全档里选总代价 cand_merged+audit_pairs
 * 最小者；若其中存在候选对数 <= (1-target)*cand_legacy 的档，
 * target_met=1 并优先在这些档里选。target 取 [0,1]。
 * bestW/best 可为 NULL。返回 SW_OK（找到安全窗口）或错误码。
 */
int sweep_choose(const SwSeg *segs, int n, double target, unsigned flags,
                 double *bestW, SwStats *best);

/* 交点表 CSV：snap,ia,ib,t,u,x,y（%.17g）。返回写入行数，失败负值。 */
int sweep_hits_csv(const SwHit *hits, int nhits, FILE *fp);

/* 写出一句拒绝/安全说明（中文），返回写入长度。 */
int sweep_format_reject(const SwStats *st, char *buf, int cap);

const char *sweep_strerror(int code);

#endif
