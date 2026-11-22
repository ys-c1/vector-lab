#ifndef SWEEP_SETS_H
#define SWEEP_SETS_H

#include "sweep.h"

/*
 * 四组线段集（每组的构造都让“逐事件处理”重复枚举、合并后应显著下降）：
 *
 *   A 规则网格：水平段在两个簇里成组起止（含完全同刻的组），竖直段按
 *     x 递增逐个进入；事件时刻分散，W=0 仍有大量重复枚举。
 *   B 近同刻簇：每个簇内水平段起点相差 0.02～0.05（浮点意义上不同时刻），
 *     逐事件处理把它们当成许多独立时刻，合并后一次性快照。
 *   C 长短混杂：簇内既有同刻长段，也有只在簇中部短暂存在的短段——
 *     它让“过大的窗口”必然漏交，用来触发拒绝路径。
 *   D 零长段为主：竖直段在扫描方向（x）上长度为 0（lo == hi），事件时间差
 *     为零；这些段与它们贡献的候选对单独统计。
 */

#define SW_SET_MAX 128

typedef struct {
    const char *name;
    int n;
    SwSeg segs[SW_SET_MAX];
} SwSegSet;

static inline void swset_A(SwSegSet *s) {
    int i = 0;
    /* 上簇：12 条水平段同时起于 x=0（完全同刻），长度交替 */
    for (int r = 0; r < 12; ++i, ++r)
        s->segs[i] = (SwSeg){0.0, (double)r, (r % 2) ? 9.0 : 14.0, (double)r};
    /* 上簇竖直段：逐个进入 */
    for (int k = 0; k < 8; ++i, ++k)
        s->segs[i] = (SwSeg){1.0 + 1.6 * k, -1.0, 1.0 + 1.6 * k, 13.0};
    /* 下簇：12 条水平段同刻起于 x=20 */
    for (int r = 0; r < 12; ++i, ++r)
        s->segs[i] = (SwSeg){20.0, 20.0 + (double)r, (r % 2) ? 31.0 : 35.0, 20.0 + (double)r};
    for (int k = 0; k < 6; ++i, ++k)
        s->segs[i] = (SwSeg){20.5 + 2.0 * k, 19.0, 20.5 + 2.0 * k, 33.0};
    /* 右侧孤立交叉对 */
    s->segs[i++] = (SwSeg){60.0, 2.0, 68.0, 7.0};
    s->segs[i++] = (SwSeg){64.0, 7.0, 72.0, 2.0};
    s->name = "A 规则网格/同刻簇";
    s->n = i;
}

static inline void swset_B(SwSegSet *s) {
    int i = 0;
    /* 三个簇，每簇 10 条水平段，起点相差 0.02（近同刻） */
    for (int band = 0; band < 3; ++band) {
        double y0 = 12.0 * band;
        for (int r = 0; r < 10; ++i, ++r)
            s->segs[i] = (SwSeg){1.0 + 0.02 * r + 5.0 * band, y0 + (double)r,
                                 (1.0 + 0.02 * r + 5.0 * band) + 11.0, y0 + (double)r};
        for (int k = 0; k < 6; ++i, ++k)
            s->segs[i] = (SwSeg){2.0 + 5.0 * band + 1.5 * k, y0 - 1.0,
                                 2.0 + 5.0 * band + 1.5 * k, y0 + 10.0};
    }
    /* 同刻尾簇 */
    for (int r = 0; r < 8; ++i, ++r)
        s->segs[i] = (SwSeg){44.0, (double)r, (r % 2) ? 52.0 : 57.0, (double)r};
    for (int k = 0; k < 4; ++i, ++k)
        s->segs[i] = (SwSeg){45.0 + 2.2 * k, -1.0, 45.0 + 2.2 * k, 9.0};
    s->segs[i++] = (SwSeg){62.0, 2.0, 70.0, 5.0};
    s->segs[i++] = (SwSeg){66.0, 5.0, 74.0, 2.0};
    s->name = "B 近同刻簇";
    s->n = i;
}

static inline void swset_C(SwSegSet *s) {
    int i = 0;
    /* 主簇：14 条同刻长水平段 */
    for (int r = 0; r < 14; ++i, ++r)
        s->segs[i] = (SwSeg){1.0, (double)r, 13.0, (double)r};
    for (int k = 0; k < 7; ++i, ++k)
        s->segs[i] = (SwSeg){2.0 + 1.4 * k, -1.0, 2.0 + 1.4 * k, 15.0};
    /* 第二个簇：起点相差 0.3，跨度很宽 */
    for (int r = 0; r < 12; ++i, ++r)
        s->segs[i] = (SwSeg){20.0 + 0.3 * r, 18.0 + (double)r,
                             (20.0 + 0.3 * r) + 14.0, 18.0 + (double)r};
    for (int k = 0; k < 6; ++i, ++k)
        s->segs[i] = (SwSeg){21.0 + 2.4 * k, 17.0, 21.0 + 2.4 * k, 31.0};
    /* 短暂存在的短段：只与第二个簇的早期段共存；它们是“过大窗口必漏交”的抓手 */
    s->segs[i++] = (SwSeg){20.4, 16.0, 23.0, 19.5};
    s->segs[i++] = (SwSeg){21.0, 20.0, 22.0, 15.5};
    s->segs[i++] = (SwSeg){20.6, 17.2, 23.4, 17.2};   /* 短水平段，穿越上述两条 */
    /* 孤立交叉对 */
    s->segs[i++] = (SwSeg){60.0, 2.0, 68.0, 7.0};
    s->segs[i++] = (SwSeg){64.0, 7.0, 72.0, 2.0};
    s->name = "C 长短混杂/短暂段";
    s->n = i;
}

static inline void swset_D(SwSegSet *s) {
    int i = 0;
    /* 12 个 x 位置，每个位置两条完全重合的竖直段（零长段，事件时间差为零） */
    for (int k = 0; k < 12; ++k) {
        double x = 2.0 + 0.9 * k;
        s->segs[i++] = (SwSeg){x, -1.0, x, 14.0};
        s->segs[i++] = (SwSeg){x, -1.0, x, 14.0};
    }
    /* 14 条水平段同时起于 x=1 */
    for (int r = 0; r < 14; ++i, ++r)
        s->segs[i] = (SwSeg){1.0, (double)r, 13.0, (double)r};
    /* 近同刻水平段（起点相差 0.03） */
    for (int r = 0; r < 12; ++i, ++r)
        s->segs[i] = (SwSeg){20.0 + 0.03 * r, 20.0 + (double)r,
                             (20.0 + 0.03 * r) + 9.0, 20.0 + (double)r};
    for (int k = 0; k < 5; ++i, ++k)
        s->segs[i] = (SwSeg){21.0 + 2.1 * k, 19.0, 21.0 + 2.1 * k, 33.0};
    s->segs[i++] = (SwSeg){40.0, 2.0, 48.0, 7.0};
    s->segs[i++] = (SwSeg){44.0, 7.0, 52.0, 2.0};
    s->name = "D 零长段为主";
    s->n = i;
}

static inline void swset_all(SwSegSet sets[4]) {
    swset_A(&sets[0]);
    swset_B(&sets[1]);
    swset_C(&sets[2]);
    swset_D(&sets[3]);
}

#endif
