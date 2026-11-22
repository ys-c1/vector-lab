#ifndef SWEEP_H
#define SWEEP_H

/* 扫描线求交：事件排序的严格全序实现。
 *
 * 排序键（字典序元组，逐级兜底，保证任意两个不同事件的键都不相等）：
 *   1. 事件位置 x
 *   2. 事件位置 y
 *   3. 事件类型 kind（LEFT=0 < CROSS=1 < RIGHT=2）
 *   4. 斜率序：段方向规范化后按斜率升序（竖直段斜率 +inf，排最后），
 *      用方向向量叉积判定，不做浮点容差
 *   5. 段号 seg_id（键 1-4 相同但段不同的事件在此分序）
 *   6. 交点事件再按 (seg_a, seg_b) 分序
 *
 * 输入校验：零长段（两端点完全相同）或重复段（端点对相同、段号不同）
 * 一律拒绝，错误结构带段号。
 */

typedef struct { double x, y; } SPoint;
typedef struct { SPoint p, q; int id; } SSeg;

typedef struct {
    SPoint at;      /* 交点坐标 */
    int seg_a;      /* 较小段号 */
    int seg_b;      /* 较大段号 */
} SHit;

typedef struct {
    int count;
    SHit *hits;     /* 顺序即事件处理顺序（stable 时为严格全序） */
} STable;

typedef enum { SEV_LEFT = 0, SEV_CROSS = 1, SEV_RIGHT = 2 } SEventKind;

typedef struct {
    double x, y;
    SEventKind kind;
    int seg_id;     /* 端点事件：本段号；交点事件：两交点段中较小段号 */
    int seg_a;      /* 交点事件有效，否则 -1 */
    int seg_b;
} SEvent;

typedef struct {
    int count;
    SEvent *events;
} SEventList;

typedef struct {
    int code;       /* 0=无错误；1=零长段；2=重复段；3=参数错误；4=内存错误 */
    int seg_a;      /* 出错段号（重复段时为先出现的段号） */
    int seg_b;      /* 重复段时为后出现的段号，否则 -1 */
    char msg[160];
} SError;

/* 求交点表。stable=1 使用严格全序键；stable=0 使用旧的不稳定排序
 * （仅比较 x/y/kind，相等即 0，配合随机洗牌）。seed 仅 stable=0 时使用，
 * seed>=0 为固定种子（可复现），seed<0 表示用运行时熵播种。 */
int sweep_table(const SSeg *segs, int n, int stable, long seed,
                STable *out, SError *err);

/* 只取排序后的事件表（排序键检查用），不做扫描线处理。 */
int sweep_events(const SSeg *segs, int n, int stable, long seed,
                 SEventList *out, SError *err);

void sweep_free_table(STable *t);
void sweep_free_events(SEventList *e);
const char *sweep_strerror(const SError *err);

#endif
