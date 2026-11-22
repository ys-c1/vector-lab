#ifndef ATLAS_H
#define ATLAS_H
/* 图集最小边长反解：给定图块集合与允许的最大浪费率，二分搜索最小可行方形图集边长。
 * 边长向上取整到 2 的幂（图集纹理的常见要求），结果中的 side 为实际使用边长。
 * 浪费率 = (S*S - 所有图块面积之和) / (S*S)，S 为向上取整后的边长。
 *
 * 关于浪费率上界：图块总面积 A 固定时，waste = 1 - A/S^2 随 S 单调递增，
 * 所以"能装下"的最小边长同时也是同组图块浪费率最低的边长。放大边长只会更浪费，
 * 绝不可能把超标的浪费率救回来。因此 max_waste 是验收上界：最小可行边长仍超标时
 * 返回 ATLAS_EWASTE，并说明该上界在当前图块集合下不可达（需改装箱/换非方形图集/
 * 拆图集，而不是继续放大）。 */
#define ATLAS_MAX_TILES 512
#define ATLAS_MAX_SIDE 16384   /* 单张图集的边长上限（常见 GPU 纹理上限量级） */

typedef struct { int w, h; } ATile;
typedef struct { int n; ATile t[ATLAS_MAX_TILES]; } Tiles;

typedef struct {
    int side;            /* 最终边长（2 的幂） */
    int side_raw;        /* 二分搜到的原始最小可行边长（未取整） */
    int pow2_rounded;    /* 1 表示 side_raw 不是 2 的幂，已向上取整 */
    int packed;          /* 是否装箱成功 */
    int pass;            /* 实际浪费率是否不超过上界 */
    int tiles;           /* 图块数 */
    long long tile_area; /* 图块总面积 */
    double waste;        /* 实际浪费率（按最终 2 的幂边长） */
    double waste_raw;    /* 原始边长 side_raw 下的浪费率，用于对比取整代价 */
    double limit;        /* 允许的浪费率上界 */
    double ms;           /* 整个反解（含二分）耗时，毫秒 */
    double pack_ms;      /* 最终边长下一次性装箱耗时，毫秒 */
    int boxes;           /* 成功时的放置数，应等于 tiles */
} AtlasResult;

/* 错误码 */
typedef enum {
    ATLAS_OK = 0,
    ATLAS_EINPUT = -1,      /* 空图集、图块尺寸非正、参数越界 */
    ATLAS_ETOOBIG = -2,     /* 有图块大于边长，放不进 */
    ATLAS_EINFEASIBLE = -3, /* 边长上限内均不可行 */
    ATLAS_ENOMEM = -4,
    ATLAS_EWASTE = -5       /* 装箱成功但实际浪费率超过上界（结果仍填好） */
} AtlasErr;

/* 堆叠构造给出的可行边长上界。 */
int atlas_side_bound(const Tiles *t);

/* 反解最小可行边长。max_waste 为允许的浪费率上界（0..1）。
 * 成功返回 ATLAS_OK；浪费率超上界返回 ATLAS_EWASTE 并在 out 中给出数据；
 * 其余错误返回对应负码。 */
int atlas_solve(const Tiles *t, double max_waste, AtlasResult *out);

/* 单个边长下的装箱可行性。成功返回 1 并把放置矩形写入 boxes
 * （每块 4 个 int：x y w h），失败返回 0，参数非法返回负错误码。 */
int atlas_pack(const Tiles *t, int side, int *boxes, int max_boxes);

/* 内置样例。 */
void atlas_sample_icons(Tiles *t);   /* 图标：小尺寸、数量多、尺寸相近 */
void atlas_sample_mixed(Tiles *t);   /* 混合尺寸：小图标 + 中面板 + 大背景 */
void atlas_sample_strips(Tiles *t);  /* 极端长条：1xN、Nx1 与细长块 */
void atlas_sample_uniform(Tiles *t); /* 全同尺寸：同一尺寸重复 */

#endif
