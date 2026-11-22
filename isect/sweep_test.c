#include "sweep.h"
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static SSeg mk(double x1, double y1, double x2, double y2, int id) {
    SSeg s = { { x1, y1 }, { x2, y2 }, id };
    return s;
}

int main(void) {
    /* 1. 基本求交与计数：2 横线 x 3 纵线 = 6 个交点 */
    SSeg grid[5] = { mk(0,0,10,0,0), mk(0,10,10,10,1),
                     mk(2,-1,2,11,2), mk(5,-1,5,11,3), mk(8,-1,8,11,4) };
    STable t; SError err;
    CK(sweep_table(grid, 5, 1, 0, &t, &err) == 0);
    CK(t.count == 6);
    sweep_free_table(&t);

    /* 2. 全序：事件两两比较非零（经由排序后相邻键严格递增验证） */
    SEventList el;
    CK(sweep_events(grid, 5, 1, 0, &el, &err) == 0);
    for (int i = 1; i < el.count; ++i) {
        const SEvent *a = &el.events[i - 1], *b = &el.events[i];
        int strictly_before =
            (a->x < b->x) ||
            (a->x == b->x && a->y < b->y) ||
            (a->x == b->x && a->y == b->y && a->kind < b->kind) ||
            (a->x == b->x && a->y == b->y && a->kind == b->kind &&
             (a->seg_id < b->seg_id ||
              (a->seg_id == b->seg_id &&
               (a->seg_a < b->seg_a ||
                (a->seg_a == b->seg_a && a->seg_b < b->seg_b)))));
        CK(strictly_before || (a->x == b->x && a->y == b->y &&
                               a->kind == b->kind && a->seg_id == b->seg_id &&
                               a->seg_a == b->seg_a && a->seg_b == b->seg_b));
    }
    sweep_free_events(&el);

    /* 3. 十次重跑输出完全一致（序列化比较） */
    char ref[8192]; size_t roff = 0;
    CK(sweep_table(grid, 5, 1, 0, &t, &err) == 0);
    for (int i = 0; i < t.count; ++i)
        roff += (size_t)snprintf(ref + roff, sizeof(ref) - roff, "%.6f %.6f %d %d\n",
                                 t.hits[i].at.x, t.hits[i].at.y,
                                 t.hits[i].seg_a, t.hits[i].seg_b);
    sweep_free_table(&t);
    for (int r = 0; r < 10; ++r) {
        CK(sweep_table(grid, 5, 1, 0, &t, &err) == 0);
        char cur[8192]; size_t coff = 0;
        for (int i = 0; i < t.count; ++i)
            coff += (size_t)snprintf(cur + coff, sizeof(cur) - coff, "%.6f %.6f %d %d\n",
                                     t.hits[i].at.x, t.hits[i].at.y,
                                     t.hits[i].seg_a, t.hits[i].seg_b);
        CK(coff == roff && memcmp(ref, cur, coff) == 0);
        sweep_free_table(&t);
    }

    /* 4. legacy 不同种子可产生不同次序：5 段全共点，10 个 CROSS 同坐标，
     * 旧键对它们比较全相等，洗牌后次序随种子变化 */
    SSeg cc_legacy[5] = { mk(0,5,10,5,0), mk(5,0,5,10,1), mk(1,1,9,9,2),
                          mk(9,1,1,9,3), mk(2,3,8,7,4) };
    STable ta, tb;
    CK(sweep_table(cc_legacy, 5, 0, 111, &ta, &err) == 0);
    CK(sweep_table(cc_legacy, 5, 0, 999, &tb, &err) == 0);
    CK(ta.count == tb.count && ta.count == 10);
    int order_differs = 0;
    for (int i = 0; i < ta.count; ++i)
        if (ta.hits[i].at.x != tb.hits[i].at.x ||
            ta.hits[i].at.y != tb.hits[i].at.y ||
            ta.hits[i].seg_a != tb.hits[i].seg_a ||
            ta.hits[i].seg_b != tb.hits[i].seg_b) { order_differs = 1; break; }
    CK(order_differs);
    sweep_free_table(&ta); sweep_free_table(&tb);

    /* 5. 零长段拒绝，指出段号 */
    SSeg zero[2] = { mk(0,0,10,0,7), mk(3,3,3,3,9) };
    CK(sweep_table(zero, 2, 1, 0, &t, &err) != 0);
    CK(err.code == 1 && err.seg_a == 9);

    /* 6. 重复段拒绝，指出两个段号（含反向端点对） */
    SSeg dup[3] = { mk(0,0,10,0,0), mk(2,-1,2,9,1), mk(10,0,0,0,2) };
    CK(sweep_table(dup, 3, 1, 0, &t, &err) != 0);
    CK(err.code == 2 && err.seg_a == 0 && err.seg_b == 2);

    /* 7. 参数错误 */
    CK(sweep_table(0, 0, 1, 0, &t, &err) != 0 && err.code == 3);

    /* 8. 共点 5 段：10 个 CROSS 同坐标，全序仍唯一 */
    SSeg cc[5] = { mk(0,5,10,5,0), mk(5,0,5,10,1), mk(1,1,9,9,2),
                   mk(9,1,1,9,3), mk(2,3,8,7,4) };
    CK(sweep_events(cc, 5, 1, 0, &el, &err) == 0);
    int crosses = 0;
    for (int i = 0; i < el.count; ++i) if (el.events[i].kind == SEV_CROSS) ++crosses;
    CK(crosses == 10);
    sweep_free_events(&el);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
