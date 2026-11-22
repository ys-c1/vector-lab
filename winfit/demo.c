#define _POSIX_C_SOURCE 199309L
#include "winfit.h"
#include <stdio.h>
#include <time.h>

/* 目标占屏区间：45% ~ 65% */
#define LO 0.45
#define HI 0.65
#define ITERS 200000

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double time_ns_per_call(int (*fn)(const WFBox *, double, double, double,
                                         double, double, WFBox *),
                               const WFBox *bb, double rt, double ux, double uy,
                               WFBox *out) {
    double t0 = now_sec();
    int rc = WF_OK;
    for (int i = 0; i < ITERS; ++i)
        rc = fn(bb, LO, HI, rt, ux, uy, out);
    double t1 = now_sec();
    if (rc != WF_OK) return -1.0;
    return (t1 - t0) * 1e9 / ITERS;
}

static void report(const char *name, const WFBox *bb, double rt,
                   double ux, double uy) {
    WFBox w;
    double ns = time_ns_per_call(winfit_fit, bb, rt, ux, uy, &w);
    double rho = winfit_occupancy(bb, &w);
    int in = winfit_in_range(bb, &w, LO, HI) == 1;
    printf("%-6s 窗口[minx miny maxx maxy]=[%8.4f %8.4f %8.4f %8.4f] "
           "占屏=%6.2f%% 区间内=%s 耗时=%.1f ns/次\n",
           name, w.minx, w.miny, w.maxx, w.maxy, rho * 100.0,
           in ? "是" : "否", ns);
}

int main(void) {
    /* 内容包围盒：100 x 40 的非对称图形，左上角原点 */
    WFBox bb = {0.0, 0.0, 100.0, 40.0};
    printf("内容盒 [minx miny maxx maxy]=[%.2f %.2f %.2f %.2f]  "
           "目标占屏区间 [%.0f%%, %.0f%%]\n",
           bb.minx, bb.miny, bb.maxx, bb.maxy, LO * 100, HI * 100);

    report("紧凑", &bb, HI,        0.5, 0.5);   /* 目标顶到区间上界，窗口最小 */
    report("居中", &bb, 0.55,      0.5, 0.5);   /* 区间中点，两侧留白均分 */
    report("偏心", &bb, 0.55,      0.85, 0.2);  /* 左/下留白占比：图形偏右下，留白偏左上 */

    /* 超界组：倒置与零面积窗口必须被拒，并点名涉事边 */
    const struct { const char *name; WFBox w; } bad[] = {
        {"横向倒置", {80.0, 0.0, 20.0, 60.0}},   /* minx > maxx */
        {"横向零宽", {20.0, 0.0, 20.0, 60.0}},   /* minx == maxx，面积为零 */
        {"纵向倒置", {0.0, 60.0, 80.0, 10.0}},   /* miny > maxy */
        {"纵向零高", {0.0, 60.0, 80.0, 60.0}},   /* miny == maxy，面积为零 */
    };
    double t0 = now_sec();
    int rc[4];
    for (int i = 0; i < 4; ++i)
        for (int k = 0; k < ITERS; ++k) rc[i] = winfit_validate(&bad[i].w);
    double ns = (now_sec() - t0) * 1e9 / (ITERS * 4);
    for (int i = 0; i < 4; ++i) {
        printf("超界  %-8s 窗口=[%7.2f %7.2f %7.2f %7.2f] 占屏=N/A "
               "code=%d %s 耗时=%.1f ns/次\n",
               bad[i].name, bad[i].w.minx, bad[i].w.miny,
               bad[i].w.maxx, bad[i].w.maxy, rc[i],
               winfit_strerror(rc[i]), ns);
    }

    /* 退化内容盒同样被拒 */
    WFBox deg = {50.0, 0.0, 50.0, 40.0};
    WFBox out;
    int r = winfit_fit(&deg, LO, HI, 0.55, 0.5, 0.5, &out);
    printf("超界  退化内容盒 code=%d %s\n", r, winfit_strerror(r));
    return 0;
}
