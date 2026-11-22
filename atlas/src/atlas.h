#ifndef ATLAS_H
#define ATLAS_H

/* 图集打包：货架式首次适应（FFDH）。
 * 同一算法支持两种插入顺序：
 *   ATLAS_ORDER_INPUT 保持调用方给定的图块顺序（"关掉新逻辑"），
 *   ATLAS_ORDER_SMART 按 面积/长宽比 重排优先级（见 atlas.c 中 comparator）。
 * 两种模式共用同一个放置器，因此关闭新逻辑时输出与原始插入顺序逐字节一致。
 */

typedef struct {
    int id;          /* 调用方编号，同尺寸图块按它定序 */
    int w, h;        /* 原始尺寸（未旋转） */
} ATile;

typedef struct {
    int id;          /* 对应 ATile.id */
    int bin;         /* 图集页号，从 0 起 */
    int x, y;        /* 页内左上角坐标 */
    int w, h;        /* 实际占用尺寸（旋转后） */
    int rotated;     /* 1 表示相对原始尺寸旋转了 90 度 */
} APlace;

typedef struct {
    APlace *items;   /* 按插入/排序后的处理顺序写入 */
    int count;
    int cap;
    int bins;        /* 使用的图集页数 */
    int atlas_w, atlas_h;
    int reject_id;   /* ATLAS_E_TOO_LARGE 时记录被拒图块编号，否则 -1 */
} AResult;

enum {
    ATLAS_OK = 0,
    ATLAS_E_ARG = -1,
    ATLAS_E_TOO_LARGE = -2,   /* 图块任一朝向都放不进单页 */
    ATLAS_E_MEM = -3
};

enum {
    ATLAS_ORDER_INPUT = 0,    /* 原插入顺序，不启用新逻辑 */
    ATLAS_ORDER_SMART = 1     /* 面积+长宽比优先级顺序 */
};

/* 排序输入数组的副本（不改原数组），返回 malloc 的新数组，调用方 free。
 * 同尺寸（含面积、归一化高、长宽比全部相等）按 id 升序。n<=0 或出错返回 NULL。 */
ATile *atlas_sorted(const ATile *tiles, int n);

/* 打包到 w*h 的图集页，放不下自动开新页。
 * 任一图块两种朝向都超出页面 -> ATLAS_E_TOO_LARGE，且 *out 不含该图块。
 * 成功返回 ATLAS_OK，结果写入 out（调用方 atlas_result_free）。 */
int atlas_pack(const ATile *tiles, int n, int w, int h, int order, AResult *out);

void atlas_result_init(AResult *r);
void atlas_result_free(AResult *r);

/* 浪费率：已开页的紧致包围盒（各页货架高度之和 * 页宽）内，
 * 未被图块覆盖的比例，范围 [0,1)。空结果返回 -1。 */
double atlas_waste(const AResult *r);

#endif
