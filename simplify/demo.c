#include "simplify.h"
#include <stdio.h>
#include <math.h>

/* 确定性伪随机（自带 LCG，不依赖 libc 实现） */
static unsigned rng_state = 20260611u;
static double frand(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) / 32768.0;
}

static int gen(int type, SPoint *p, int n) {
    for (int i = 0; i < n; ++i) {
        double t = (double)i / (n - 1);
        switch (type) {
        case 0: /* 直线加噪 */
            p[i].x = t * 100.0;
            p[i].y = t * 20.0 + (frand() - 0.5) * 2.0;
            break;
        case 1: /* 圆弧 */
            p[i].x = 50.0 + 40.0 * cos(t * 3.6);
            p[i].y = 50.0 + 40.0 * sin(t * 3.6);
            break;
        case 2: /* 正弦波 */
            p[i].x = t * 100.0;
            p[i].y = 30.0 * sin(t * 12.0);
            break;
        default: /* 随机游走 */
            p[i].x = i ? p[i-1].x + (frand() - 0.5) * 6.0 : 0.0;
            p[i].y = i ? p[i-1].y + (frand() - 0.5) * 6.0 : 0.0;
            break;
        }
    }
    return n;
}

int main(void) {
    static const char *names[4] = {"直线加噪", "圆弧", "正弦波", "随机游走"};
    static const int ns[4] = {240, 200, 260, 300};
    SPoint path[SP_MAX_PTS];
    SPoly out, legacy;
    SStats st;

    printf("DP 分段抽稀（segments=8, max_dev=3.0）\n");
    printf("%-10s %6s %6s %10s %10s   %s\n",
           "路径", "分段数", "保留点", "总偏差", "耗时ms", "分段边界");
    for (int t = 0; t < 4; ++t) {
        int n = gen(t, path, ns[t]);
        SOpts opt = {1, 0.0, 8, 3.0};
        int rc = sp_simplify(path, n, &opt, &out, &st);
        if (rc != SP_OK) { printf("%-10s 拒绝：%s\n", names[t], sp_strerror(rc)); continue; }
        printf("%-10s %6d %6d %10.4f %10.3f   ",
               names[t], st.segments, st.kept, st.total_dev, st.elapsed_ms);
        for (int k = 0; k <= st.segments; ++k) printf("%d ", st.boundary[k]);
        printf("\n");
    }

    printf("\n旧全局抽稀对照（eps=3.0，长路径容易全留或全丢）\n");
    printf("%-10s %6s %6s\n", "路径", "原点数", "保留点");
    for (int t = 0; t < 4; ++t) {
        int n = gen(t, path, ns[t]);
        if (sp_simplify_global(path, n, 3.0, &legacy) != SP_OK) return 1;
        printf("%-10s %6d %6d\n", names[t], n, legacy.count);
    }

    printf("\n拒绝示例\n");
    SOpts bad0 = {1, 0.0, 0, 1.0};
    int rc = sp_simplify(path, 100, &bad0, &out, &st);
    printf("段数为零 -> rc=%d %s\n", rc, sp_strerror(rc));
    SOpts ok = {1, 0.0, 4, 1.0};
    rc = sp_simplify(path, 1, &ok, &out, &st);
    printf("点不足二 -> rc=%d %s\n", rc, sp_strerror(rc));

    printf("\n关掉新逻辑（use_dp=0）与旧全局抽稀逐字节一致\n");
    int n = gen(3, path, ns[3]);
    SOpts off = {0, 0.5, 0, 0.0};
    SPoly wrapped;
    sp_simplify_global(path, n, 0.5, &legacy);
    sp_simplify(path, n, &off, &wrapped, &st);
    int same = legacy.count == wrapped.count;
    for (int i = 0; same && i < legacy.count; ++i)
        if (legacy.idx[i] != wrapped.idx[i] ||
            legacy.pts[i].x != wrapped.pts[i].x ||
            legacy.pts[i].y != wrapped.pts[i].y) same = 0;
    printf("legacy kept=%d, use_dp=0 kept=%d, 逐字节一致: %s\n",
           legacy.count, wrapped.count, same ? "是" : "否");
    return same ? 0 : 1;
}
