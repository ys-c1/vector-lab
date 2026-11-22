#ifndef PEEL_H
#define PEEL_H

/* 凸层逐层剥离（洋葱剥皮）。
 *
 * 每一层取当前点集的凸包，记录该层面积，再把落在凸包边界上的点
 * （凸包顶点 + 恰好落在边上的共线点）移出，对剩余点重复。层号从 1 开始。
 * 由于剩余点集是上一层点集的子集，其凸包包含于上层凸包，面积随层号
 * 单调不增；实现里仍逐层核对，发现回升即报内部错误。
 *
 * 去重：输入点先按坐标精确去重（x、y 都相等的视为同一点），
 * “移除”与“剩余”一律按去重后的点计数。
 *
 * 拒绝规则（都带层号）：
 *   - 进入某层时剩余点不足三点        -> PEEL_STOP_FEW
 *   - 剩余点全共线，成不了面积        -> PEEL_STOP_COLLINEAR
 *   - 层数超过调用方给的 layer_cap    -> PEEL_STOP_CAPACITY
 * 首层就停止时 layer_count 为 0，peel_run 返回 2（共线）或 3（不足三点）；
 * 已经剥出至少一层时返回 0，停止原因放在 summary 里。
 */

#define PEEL_MAX_POINTS 4096   /* 与 gold/ref 的 MAXP 一致 */
#define PEEL_MAX_LAYERS 256

typedef struct { double x, y; } PPoint;

typedef struct {
    int layer;        /* 1 起的层号 */
    int input_unique; /* 进入本层的去重点数 */
    int hull_verts;   /* 本层凸包的严格顶点数（边上共线点不算顶点） */
    int removed;      /* 本层移出的边界点数（含外套共线点） */
    double area;      /* 本层凸包面积，非负 */
    double ms;        /* 本层剥离耗时（毫秒，墙钟） */
} PLayer;

typedef enum {
    PEEL_STOP_DONE = 0,
    PEEL_STOP_FEW = 1,
    PEEL_STOP_COLLINEAR = 2,
    PEEL_STOP_CAPACITY = 3,
    PEEL_STOP_INTERNAL = 4
} PStopReason;

typedef struct {
    int layer;      /* 被拒绝/终止的层号（1 起） */
    int remaining;  /* 该层开始时去重后的剩余点数 */
    PStopReason reason;
} PStop;

typedef struct {
    int input_points;  /* 原始点数（含重复） */
    int unique_points; /* 去重后点数 */
    int layer_count;   /* 成功剥出的层数 */
    PStop stop;
} PSummary;

/* 每完成一层回调一次；rem/hull 只在回调期间有效，调用方自行落盘。
 * hull 是严格顶点、逆时针；rem 是进入本层的去重剩余点。
 * 回调返回值：0 继续，非 0 中止（peel_run 原样返回该值）。 */
typedef int (*PLayerFn)(void *ctx, const PPoint *rem, int nrem,
                        const PPoint *hull, int nhull, const PLayer *info);

typedef struct {
    PLayer *layers;    /* 调用方提供，长度至少 layer_cap */
    int layer_cap;
    PLayerFn on_layer; /* 可为 NULL */
    void *ctx;
} PPeelOut;

/* 返回 0 成功（至少一层）；2 首层全共线；3 首层不足三点；
 * -1 参数错误或坐标非有限值；-4 层数超出 layer_cap；
 * -5 内部异常（某层没移出任何点，或面积回升）。
 * 回调返回非 0 时，peel_run 停止并原样返回该值。 */
int peel_run(const PPoint *pts, int n, PPeelOut *out, PSummary *sum);

/* 反解：剥离第 1..k 层后，剩余面积占第 1 层面积的比 <= 1-frac。
 * 返回最小的 k（0..nlayers；frac<=0 时返回 0）。层数据无效返回 -1；
 * 到最后一层仍达不到返回 -2（正常数据不会发生：末层后剩余面积按 0 计）。
 * ratio 非 NULL 时写入该 k 对应的剩余比例。 */
int peel_cover_layer(const PLayer *layers, int nlayers, double frac, double *ratio);

/* 多边形面积（鞋带公式取绝对值）。 */
double peel_area(const PPoint *poly, int n);

#endif
