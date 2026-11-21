/* 容差扩展求交基准：128 / 1024 段两档，统计漏判、误判、耗时与误差，
 * 并与逐段手判接触表核对。
 *
 * 场景构造（手判依据）：段 i 的 A 段为 (i*100,0)->(i*100+50,0)，
 * B 段为 x = i*100+50+g_i 处 y∈[-5,5] 的竖段，g_i 按
 * {0, 0.5, 1.0, 1.5, 2.0, 3.0, 10.0} 循环。
 * 手判真值：g_i <= tol 即接触（A 端点外延 tol 恰好够到 B）。
 * 相邻段间隔 >= 47px，大于 2*tol 上限，不会产生跨段干扰。
 * 误差定义：命中点到真实接触点（两端点最近点对中点）的距离。 */
#define _POSIX_C_SOURCE 199309L
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

typedef struct { IPoint p0, p1; } Seg;

static const double GAPS[] = {0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 10.0};
#define NGAPS ((int)(sizeof(GAPS) / sizeof(GAPS[0])))
static const double TOLS[] = {0.0, 0.5, 1.0, 1.5, 2.0};
#define NTOLS ((int)(sizeof(TOLS) / sizeof(TOLS[0])))
#define REPS 3

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void build(int n, Seg *a, Seg *b) {
    for (int i = 0; i < n; ++i) {
        double g = GAPS[i % NGAPS];
        a[i].p0 = (IPoint){i * 100.0, 0.0};
        a[i].p1 = (IPoint){i * 100.0 + 50.0, 0.0};
        b[i].p0 = (IPoint){i * 100.0 + 50.0 + g, -5.0};
        b[i].p1 = (IPoint){i * 100.0 + 50.0 + g, 5.0};
    }
}

static int truth(int i, double tol) { return GAPS[i % NGAPS] <= tol; }

int main(void) {
    const int Ns[] = {128, 1024};
    int bad = 0;

    printf("手判接触表（构造依据）：间隙循环 ");
    for (int k = 0; k < NGAPS; ++k) printf("%g ", GAPS[k]);
    printf("px；容差档 tol 下段 i 接触 <=> 间隙 <= tol\n");

    for (int s = 0; s < 2; ++s) {
        int n = Ns[s];
        Seg *a = malloc(sizeof(Seg) * n);
        Seg *b = malloc(sizeof(Seg) * n);
        if (!a || !b) return 2;
        build(n, a, b);

        printf("\n=== 段数 %d（%d 对段，全对全比较 %d 对）===\n", n, n, n * n);
        printf("%-8s | %-6s | %-8s | %-8s | %-6s | %-6s | %-10s | %-8s | %-8s\n",
               "容差档", "段数", "预期接触", "实测接触", "漏判", "误判",
               "耗时(ms)", "平均误差", "最大误差");
        for (int ti = 0; ti < NTOLS; ++ti) {
            double tol = TOLS[ti];
            int expect = 0;
            for (int i = 0; i < n; ++i) expect += truth(i, tol);

            int hits = 0, miss = 0, fp = 0;
            double err_sum = 0.0, err_max = 0.0, best = 1e30;
            for (int rep = 0; rep < REPS; ++rep) {
                hits = 0; miss = 0; fp = 0;
                err_sum = 0.0; err_max = 0.0;
                double t0 = now_ms();
                for (int i = 0; i < n; ++i) {
                    for (int j = 0; j < n; ++j) {
                        IHit h;
                        int r = isect_seg_tol(a[i].p0, a[i].p1,
                                              b[j].p0, b[j].p1, tol, &h);
                        if (r < 0) { fprintf(stderr, "unexpected error %d\n", r); return 2; }
                        int exp = (i == j) && truth(i, tol);
                        if (r == 1) {
                            ++hits;
                            if (!exp) ++fp;
                            else {
                                double g = GAPS[i % NGAPS];
                                double cx = i * 100.0 + 50.0 + g / 2.0;
                                double e = hypot(h.at.x - cx, h.at.y);
                                err_sum += e;
                                if (e > err_max) err_max = e;
                            }
                        } else if (exp) ++miss;
                    }
                }
                double dt = now_ms() - t0;
                if (dt < best) best = dt;
            }
            int nerr = expect - miss;
            printf("%-8.2f | %-6d | %-8d | %-8d | %-6d | %-6d | %-10.3f | %-8.4f | %-8.4f\n",
                   tol, n, expect, hits, miss, fp, best,
                   nerr ? err_sum / nerr : 0.0, err_max);
            if (miss || fp) bad = 1;
        }

        /* 逐段核对：每段 × 每容差档与手判表比对，逐条列出前 16 段 */
        int mism = 0;
        for (int i = 0; i < n; ++i)
            for (int ti = 0; ti < NTOLS; ++ti) {
                int r = isect_seg_tol(a[i].p0, a[i].p1, b[i].p0, b[i].p1,
                                      TOLS[ti], 0);
                if (r != truth(i, TOLS[ti])) {
                    ++mism;
                    printf("不一致 段号 %d 容差档 %.2f 预期 %d 实测 %d\n",
                           i, TOLS[ti], truth(i, TOLS[ti]), r);
                }
            }
        int show = n < 16 ? n : 16;
        printf("逐段手判核对（前 %d 段，每格=各容差档 预期/实测）：\n", show);
        printf("%-6s | %-6s |", "段号", "间隙");
        for (int ti = 0; ti < NTOLS; ++ti) printf(" tol=%-4.1f |", TOLS[ti]);
        printf("\n");
        for (int i = 0; i < show; ++i) {
            printf("%-6d | %-6.1f |", i, GAPS[i % NGAPS]);
            for (int ti = 0; ti < NTOLS; ++ti) {
                int r = isect_seg_tol(a[i].p0, a[i].p1, b[i].p0, b[i].p1,
                                      TOLS[ti], 0);
                printf("  %d/%d %-3s |", truth(i, TOLS[ti]), r,
                       r == truth(i, TOLS[ti]) ? "" : "!!");
            }
            printf("\n");
        }
        printf("逐段核对结果：%d 段 x %d 容差档，不一致 %d 条\n",
               n, NTOLS, mism);
        if (mism) bad = 1;
        free(a);
        free(b);
    }
    return bad;
}
