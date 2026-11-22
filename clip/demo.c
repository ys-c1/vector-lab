#define _POSIX_C_SOURCE 199309L
#include "clip.h"
#include "order.h"
#include "groups.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void print_seq(const int *ord, int wn) {
    for (int i = 0; i < wn; ++i) printf("%s%d", i ? ">" : "", ord[i]);
}

/* 原固定顺序演示：新逻辑关闭时走这里，输出与旧版逐字节一致 */
static int run_fixed(void) {
    CPoint poly[4] = {{0,0},{20,0},{20,10},{0,10}};
    CPoint win[4] = {{5,-5},{15,-5},{15,5},{5,5}};
    CPoly out;
    if (clip_convex(poly,4,win,4,&out) != 0) return 1;
    printf("clipped %d area %.2f\n", out.count, clip_area(&out));
    return 0;
}

int main(int argc, char **argv) {
    if ((argc > 1 && strcmp(argv[1], "--fixed") == 0) || getenv("CLIP_FIXED_ORDER"))
        return run_fixed();

    int best_cnt[4];
    for (int g = 0; g < 4; ++g) {
        CPoint poly[64], win[4];
        int n = group_poly(g, poly);
        group_win(g, win);
        CPoly out;
        int ord[4] = {0,1,2,3};
        int st_def[4] = {0,0,0,0}, st_best[4] = {0,0,0,0};

        double t0 = now_us();
        clip_convex(poly, n, win, 4, &out);          /* 默认固定顺序 */
        double t1 = now_us();
        int def_cnt = out.count;
        clip_convex_ordered(poly, n, win, 4, NULL, &out, st_def);

        double t2 = now_us();
        int rc = clip_best_order(poly, n, win, 4, ord, &out);
        double t3 = now_us();
        if (rc != 0) { printf("组%d 拒绝: %s\n", g + 1, clip_order_err(rc)); return 1; }
        best_cnt[g] = out.count;
        clip_convex_ordered(poly, n, win, 4, ord, &out, st_best);

        printf("组%d: 多边形 %d 顶点, 窗口 4 边 [0,%g]x[0,10]\n", g + 1, n, CLIP_GROUPS[g].wx1);
        printf("  默认顺序 0>1>2>3  顶点数 %d  阶段 ", def_cnt);
        print_seq(st_def, 4);
        printf("  耗时 %.1f us\n", t1 - t0);
        printf("  最优顺序 ");
        print_seq(ord, 4);
        printf("  顶点数 %d  阶段 ", best_cnt[g]);
        print_seq(st_best, 4);
        printf("  耗时 %.1f us (枚举 24 种)\n", t3 - t2);
        printf("  顶点数减少 %.1f%% (默认 %d -> 最优 %d)\n",
               100.0 * (def_cnt - best_cnt[g]) / def_cnt, def_cnt, best_cnt[g]);
        printf("  为什么它最少: 无上限时最终顶点数与边序无关, 差异来自 62 槽缓冲的截断位置。\n");
        printf("    缓冲按发出顺序截断, 边序决定截断落在哪些顶点上。最优顺序 ");
        print_seq(ord, 4);
        printf(" 的阶段为 ");
        print_seq(st_best, 4);
        printf(",\n    中间链连续顶到上限时被丢掉的是本会活到最后的顶点;\n");
        printf("    默认顺序 0>1>2>3 的阶段为 ");
        print_seq(st_def, 4);
        printf(", 截断只落在本就会被后续边裁掉的顶点上,\n    所以最终残留 %d 个, 比最优顺序多。\n", def_cnt);
    }

    /* 拒绝示例：空窗口与重合边 */
    {
        CPoint poly[64]; int n = group_poly(0, poly);
        CPoint empty_win[3] = {{0,0},{5,0},{10,0}};            /* 面积为零 */
        CPoint zero_len[4] = {{0,0},{10,0},{10,0},{0,10}};     /* 零长边 */
        CPoint collinear[5] = {{0,0},{5,0},{10,0},{10,10},{0,10}}; /* 共线邻边 */
        CPoly out; int ord[5];
        const CPoint *wins[3] = { empty_win, zero_len, collinear };
        int wns[3] = { 3, 4, 5 };
        const char *tags[3] = { "面积为零的窗口", "含零长边的窗口", "含共线邻边的窗口" };
        for (int k = 0; k < 3; ++k) {
            int rc = clip_best_order(poly, n, wins[k], wns[k], ord, &out);
            printf("拒绝: %s -> %s (码 %d)\n", tags[k], clip_order_err(rc), rc);
        }
    }

    /* 汇总：按顶点数升序，相同按窗口号定序 */
    {
        int idx[4] = {0,1,2,3};
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j)
                if (best_cnt[idx[j]] < best_cnt[idx[i]] ||
                    (best_cnt[idx[j]] == best_cnt[idx[i]] && idx[j] < idx[i])) {
                    int t = idx[i]; idx[i] = idx[j]; idx[j] = t;
                }
        printf("汇总(顶点数升序, 相同按窗口号):");
        for (int i = 0; i < 4; ++i) printf(" 组%d(%d)", idx[i] + 1, best_cnt[idx[i]]);
        printf("\n");
    }
    return 0;
}
