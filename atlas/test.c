#include "src/atlas.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int overlap(const APlace *a, const APlace *b) {
    if (a->bin != b->bin) return 0;
    return a->x < b->x + b->w && b->x < a->x + a->w &&
           a->y < b->y + b->h && b->y < a->y + a->h;
}

static void check_layout_valid(const AResult *r, int W, int H) {
    for (int i = 0; i < r->count; ++i) {
        const APlace *p = &r->items[i];
        CK(p->x >= 0 && p->y >= 0);
        CK(p->x + p->w <= W && p->y + p->h <= H);
        CK(p->bin >= 0 && p->bin < r->bins);
        for (int j = i + 1; j < r->count; ++j)
            CK(!overlap(p, &r->items[j]));
    }
}

int main(void) {
    /* 1. 图块大于图集：两种朝向都放不下 -> 拒判 */
    ATile big[1] = {{0, 200, 10}};
    AResult rb;
    CK(atlas_pack(big, 1, 128, 128, ATLAS_ORDER_SMART, &rb) == ATLAS_E_TOO_LARGE);
    atlas_result_free(&rb);
    /* 旋转后能放下则不拒：128x10 旋转为 10x128 */
    ATile rot[1] = {{0, 128, 10}};
    AResult rr;
    CK(atlas_pack(rot, 1, 128, 128, ATLAS_ORDER_SMART, &rr) == ATLAS_OK);
    CK(rr.items[0].rotated == 1 && rr.items[0].w == 10 && rr.items[0].h == 128);
    atlas_result_free(&rr);

    /* 2. 同尺寸图块按编号定序：smart 模式处理顺序必须是 id 升序 */
    ATile same[4] = {{7, 20, 20}, {2, 20, 20}, {9, 20, 20}, {1, 20, 20}};
    AResult rs;
    CK(atlas_pack(same, 4, 128, 128, ATLAS_ORDER_SMART, &rs) == ATLAS_OK);
    CK(rs.items[0].id == 1 && rs.items[1].id == 2 &&
       rs.items[2].id == 7 && rs.items[3].id == 9);
    atlas_result_free(&rs);

    /* 3. 关掉新逻辑：布局与原插入顺序逐字节一致（结构比较 + 确定性重复运行） */
    ATile mix[5] = {{0, 60, 16}, {1, 16, 60}, {2, 40, 40}, {3, 24, 12}, {4, 12, 24}};
    AResult r1, r2;
    CK(atlas_pack(mix, 5, 128, 128, ATLAS_ORDER_INPUT, &r1) == ATLAS_OK);
    CK(atlas_pack(mix, 5, 128, 128, ATLAS_ORDER_INPUT, &r2) == ATLAS_OK);
    CK(r1.count == r2.count && r1.bins == r2.bins);
    CK(memcmp(r1.items, r2.items, sizeof(APlace) * (size_t)r1.count) == 0);
    /* INPUT 模式不得重排：items 的 id 序列就是输入序列 */
    for (int i = 0; i < r1.count; ++i) CK(r1.items[i].id == mix[i].id);
    check_layout_valid(&r1, 128, 128);
    atlas_result_free(&r1); atlas_result_free(&r2);

    /* 4. smart 模式同样合法，且浪费率不高于 INPUT */
    AResult ri, rn;
    CK(atlas_pack(mix, 5, 128, 128, ATLAS_ORDER_INPUT, &ri) == ATLAS_OK);
    CK(atlas_pack(mix, 5, 128, 128, ATLAS_ORDER_SMART, &rn) == ATLAS_OK);
    check_layout_valid(&rn, 128, 128);
    CK(atlas_waste(&rn) <= atlas_waste(&ri) + 1e-12);
    CK(atlas_waste(&rn) >= 0.0 && atlas_waste(&rn) < 1.0);
    atlas_result_free(&ri); atlas_result_free(&rn);

    /* 5. 浪费率上界：紧致密度 >= 1/2（最后一个货架除外的经典 FFDH 界）。
     * 对任意随机实例，waste <= 1/2 + hmax/(2L) 应成立。 */
    srand(20261123);
    for (int trial = 0; trial < 2000; ++trial) {
        int n = 1 + rand() % 20;
        ATile t[24];
        for (int i = 0; i < n; ++i)
            t[i] = (ATile){i, 4 + rand() % 120, 4 + rand() % 120};
        AResult r;
        int rc = atlas_pack(t, n, 128, 128, ATLAS_ORDER_SMART, &r);
        if (rc == ATLAS_E_TOO_LARGE) continue;
        CK(rc == ATLAS_OK);
        check_layout_valid(&r, 128, 128);
        /* 重建每页紧致高度与最大货架高，验证 L <= 2A/W + hmax */
        for (int b = 0; b < r.bins; ++b) {
            long A = 0, Lnum = 0; int hmax = 0;
            for (int i = 0; i < r.count; ++i) {
                const APlace *p = &r.items[i];
                if (p->bin != b) continue;
                A += (long)p->w * p->h;
                long bot = (long)p->y + p->h;
                if (bot > Lnum) Lnum = bot;
                if (p->h > hmax) hmax = p->h;
            }
            /* L*W <= 2A + hmax*W */
            CK(Lnum * 128 <= 2 * A + (long)hmax * 128);
        }
        atlas_result_free(&r);
    }

    /* 6. 参数校验 */
    CK(atlas_pack(NULL, 1, 10, 10, ATLAS_ORDER_INPUT, &rr) == ATLAS_E_ARG);
    CK(atlas_pack(mix, 5, 0, 10, ATLAS_ORDER_INPUT, &rr) == ATLAS_E_ARG);
    CK(atlas_pack(mix, 5, 10, 10, 99, &rr) == ATLAS_E_ARG);
    CK(atlas_sorted(NULL, 3) == NULL);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
