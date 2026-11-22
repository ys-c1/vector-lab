#include "simplify.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 确定性伪随机（自带 LCG，不依赖 libc 实现） */
static unsigned rng_state = 12345u;
static double frand(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) / 32768.0;
}

static int poly_equal(const SPoly *a, const SPoly *b) {
    return a->count == b->count &&
           memcmp(a->pts, b->pts, sizeof(SPoint) * (size_t)a->count) == 0 &&
           memcmp(a->idx, b->idx, sizeof(int) * (size_t)a->count) == 0;
}

int main(void) {
    /* 旧逻辑基本行为 */
    SPoint line[3] = {{0,0},{1,0},{2,0}};
    SPoly out;
    CK(sp_simplify_global(line, 3, 0.1, &out) == SP_OK);
    CK(out.count == 2 && out.idx[0] == 0 && out.idx[1] == 2);
    CK(sp_simplify_global(line, 1, 0.1, &out) == SP_ERR_NPTS);
    CK(sp_simplify_global(NULL, 3, 0.1, &out) == SP_ERR_ARG);
    CK(sp_simplify_global(line, 3, -1.0, &out) == SP_ERR_DEV);

    /* 构造一条长路径 */
    SPoint path[200];
    for (int i = 0; i < 200; ++i) {
        path[i].x = i * 1.0;
        path[i].y = 10.0 * (i % 7) / 7.0 + frand() * 0.5;
    }

    /* 拒绝：段数为零、点不足二 */
    SOpts opt = {1, 0.0, 0, 1.0};
    SStats st;
    CK(sp_simplify(path, 200, &opt, &out, &st) == SP_ERR_SEG0);
    CK(sp_simplify(path, 1, &opt, &out, &st) == SP_ERR_NPTS);
    opt.segments = 500;
    CK(sp_simplify(path, 200, &opt, &out, &st) == SP_ERR_SEGN);
    opt.segments = 8; opt.max_dev = -0.1;
    CK(sp_simplify(path, 200, &opt, &out, &st) == SP_ERR_DEV);
    CK(sp_strerror(SP_ERR_SEG0)[0] != '\0');
    CK(sp_strerror(SP_ERR_NPTS)[0] != '\0');

    /* DP 基本性质 */
    opt.segments = 8; opt.max_dev = 2.0;
    CK(sp_simplify(path, 200, &opt, &out, &st) == SP_OK);
    CK(st.segments == 8);
    CK(st.kept == out.count);
    CK(st.kept >= 2 && st.kept <= 200);
    CK(st.total_dev <= 2.0 + 1e-9);            /* 总偏差不超过上界 */
    CK(st.elapsed_ms >= 0.0);
    CK(st.boundary[0] == 0 && st.boundary[8] == 199);
    int incr = 1;
    for (int k = 1; k <= 8; ++k) if (st.boundary[k] <= st.boundary[k-1]) incr = 0;
    CK(incr);                                   /* 边界严格递增 */
    CK(out.idx[0] == 0 && out.idx[out.count-1] == 199); /* 端点保留 */
    int ordered = 1;
    for (int i = 1; i < out.count; ++i) if (out.idx[i] <= out.idx[i-1]) ordered = 0;
    CK(ordered);

    /* 分段边界与输出可复现：连跑两次逐字节一致 */
    SPoly out2; SStats st2;
    CK(sp_simplify(path, 200, &opt, &out2, &st2) == SP_OK);
    CK(memcmp(st.boundary, st2.boundary, sizeof(int) * 9) == 0);
    CK(poly_equal(&out, &out2));

    /* max_dev=0 时全保留 */
    opt.max_dev = 0.0;
    CK(sp_simplify(path, 200, &opt, &out2, &st2) == SP_OK);
    CK(out2.count == 200 && st2.total_dev == 0.0);

    /* 共线路径：每段只留端点，总偏差≈0 */
    SPoint straight[50];
    for (int i = 0; i < 50; ++i) { straight[i].x = i; straight[i].y = 2.0 * i; }
    opt.segments = 5; opt.max_dev = 1e-9;
    CK(sp_simplify(straight, 50, &opt, &out2, &st2) == SP_OK);
    CK(st2.total_dev < 1e-8);
    CK(out2.count == 6);                        /* 5 段共享端点：5+1 个点 */

    /* 关掉新逻辑：与旧全局抽稀逐字节一致 */
    SPoly legacy, wrapped;
    opt.use_dp = 0; opt.eps = 0.3;
    CK(sp_simplify_global(path, 200, opt.eps, &legacy) == SP_OK);
    CK(sp_simplify(path, 200, &opt, &wrapped, &st) == SP_OK);
    CK(poly_equal(&legacy, &wrapped));          /* 逐字节不变 */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
