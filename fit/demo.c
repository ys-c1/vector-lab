#define _POSIX_C_SOURCE 199309L
#include "fit.h"
#include <stdio.h>
#include <time.h>

/* 从已知三次贝塞尔采样必经点（模拟美术沿曲线标的点）。 */
static FPoint cubic_at(const FPoint c[4], double t) {
    double u = 1.0 - t;
    double a = u * u * u, b = 3 * u * u * t, d = 3 * u * t * t, e = t * t * t;
    FPoint p;
    p.x = a * c[0].x + b * c[1].x + d * c[2].x + e * c[3].x;
    p.y = a * c[0].y + b * c[1].y + d * c[2].y + e * c[3].y;
    return p;
}

static void sample(const FPoint c[4], int n, FPoint *out) {
    for (int i = 0; i < n; ++i) out[i] = cubic_at(c, (double)i / (n - 1));
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static void report(const char *name, const FPoint *pts, int n) {
    FFit segs[FIT_MAX_SEG];
    double t0 = now_us();
    int nseg = fit_spline(pts, n, FIT_TOL, segs, FIT_MAX_SEG);
    double us = now_us() - t0;
    printf("[%s] n=%d\n", name, n);
    if (nseg == -1) {
        printf("  拒绝：点位少于 2，无法确定一条曲线（至少需要起点和终点）\n");
        return;
    }
    if (nseg < 0) {
        printf("  拒绝：拟合失败 (rc=%d)\n", nseg);
        return;
    }
    double worst = 0.0;
    for (int s = 0; s < nseg; ++s) {
        FFit *f = &segs[s];
        if (f->max_err > worst) worst = f->max_err;
        const char *kind = f->degenerate == 1 ? "共线退化（按直线处理）"
                         : f->degenerate == 2 ? "全部重合（曲线缩为点）"
                         : "正常拟合";
        if (nseg > 1) printf("  段 %d：\n", s);
        printf("  控制点 P0 = (%.4f, %.4f)\n", f->ctrl[0].x, f->ctrl[0].y);
        printf("  控制点 P1 = (%.4f, %.4f)\n", f->ctrl[1].x, f->ctrl[1].y);
        printf("  控制点 P2 = (%.4f, %.4f)\n", f->ctrl[2].x, f->ctrl[2].y);
        printf("  控制点 P3 = (%.4f, %.4f)\n", f->ctrl[3].x, f->ctrl[3].y);
        printf("  本段最大残差 = %.6f px，退化处理 = %s\n", f->max_err, kind);
    }
    printf("  段数 = %d，最大残差 = %.6f px（限差 0.5 px：%s）\n",
           nseg, worst, worst <= FIT_TOL ? "通过" : "超限");
    printf("  耗时 = %.1f us\n", us);
}

int main(void) {
    /* 组 1：S 形曲线，8 个必经点 */
    FPoint g1src[4] = {{0, 0}, {30, 60}, {70, -20}, {100, 40}};
    FPoint g1[8];
    sample(g1src, 8, g1);

    /* 组 2：拱形曲线，6 个必经点 */
    FPoint g2src[4] = {{0, 0}, {10, 50}, {90, 50}, {100, 0}};
    FPoint g2[6];
    sample(g2src, 6, g2);

    /* 组 3：共线点位（退化情形） */
    FPoint g3[5] = {{0, 0}, {25, 10}, {50, 20}, {75, 30}, {100, 40}};

    /* 组 4：两段不同曲率的复合曲线（单段放不下，演示自动分段） */
    FPoint a[4] = {{0, 0}, {10, 40}, {30, 50}, {45, 30}};
    FPoint b[4] = {{45, 30}, {60, 10}, {80, 0}, {100, 20}};
    FPoint g4[9];
    for (int i = 0; i < 4; ++i) g4[i] = cubic_at(a, (double)i / 4);
    for (int i = 0; i < 5; ++i) g4[4 + i] = cubic_at(b, (double)i / 4);

    /* 组 5：单点，演示拒绝 */
    FPoint g5[1] = {{10, 10}};

    report("组1 S形", g1, 8);
    report("组2 拱形", g2, 6);
    report("组3 共线", g3, 5);
    report("组4 复合", g4, 9);
    report("组5 单点", g5, 1);
    return 0;
}
