/* 反解采样密度演示：缓弯 / 急弯 / 螺旋 / 近退化四类曲线。
 * 每类报告：反解间隔、实际插值误差、表长、耗时；任何一处误差超上界即判不过。
 * 末尾两个错误用例：间隔大于总长（拒绝）、表长为零/点少于二（报错）。
 */
#define _POSIX_C_SOURCE 199309L
#include "adapt.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NP 2001          /* 曲线离散点数 */
#define PROBES 20000     /* 验证用密集弧长网格 */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void run_case(const char *name, const APoint *pts, double eps) {
    AdaptConfig cfg = { eps };
    AdaptiveTable t;
    double t0 = now_sec();
    int rc = adapt_build(pts, NP, &cfg, &t);
    double t1 = now_sec();
    if (rc != 0) {
        printf("[%s] build 失败 rc=%d\n", name, rc);
        return;
    }
    double t2 = now_sec();
    double err = adapt_verify(pts, NP, &t, PROBES);
    double t3 = now_sec();
    int pass = (err >= 0.0 && err <= eps);
    printf("[%s] eps=%.3g 反解间隔=%.6f 实际误差=%.3e 上界=%.3e 表长=%d "
           "建表=%.3fms 验证=%.3fms => %s\n",
           name, eps, t.max_interval, err, eps, t.count,
           (t1 - t0) * 1e3, (t3 - t2) * 1e3,
           pass ? "通过" : "未过(误差超上界)");
    adapt_free(&t);
}

int main(void) {
    static APoint pts[4][NP];

    /* 1) 缓弯：大半径圆弧 R=100，60 度 */
    for (int i = 0; i < NP; ++i) {
        double a = (M_PI / 3.0) * i / (NP - 1);
        pts[0][i].x = 100.0 * cos(a);
        pts[0][i].y = 100.0 * sin(a);
    }
    /* 2) 急弯：小半径圆弧 R=2，180 度 */
    for (int i = 0; i < NP; ++i) {
        double a = M_PI * i / (NP - 1);
        pts[1][i].x = 2.0 * cos(a);
        pts[1][i].y = 2.0 * sin(a);
    }
    /* 3) 螺旋：阿基米德螺线 r=0.5+0.08*theta，三圈 */
    for (int i = 0; i < NP; ++i) {
        double th = 6.0 * M_PI * i / (NP - 1);
        double r = 0.5 + 0.08 * th;
        pts[2][i].x = r * cos(th);
        pts[2][i].y = r * sin(th);
    }
    /* 4) 近退化：近乎共线折线，中点横向偏离仅 1e-3（相对长度 100） */
    for (int i = 0; i < NP; ++i) {
        double u = (double)i / (NP - 1);
        pts[3][i].x = 100.0 * u;
        pts[3][i].y = 1e-3 * (u <= 0.5 ? 2.0 * u : 2.0 * (1.0 - u));
    }

    run_case("缓弯 R=100",   pts[0], 0.01);
    run_case("急弯 R=2",     pts[1], 0.01);
    run_case("螺旋",         pts[2], 0.005);
    run_case("近退化",       pts[3], 1e-8);

    /* 错误用例一：反解间隔大于总长，必须拒绝（-2）。
     * (a) 近退化曲线配宽松 eps：该折线实测 k≈8e-4，拒绝阈值
     *     eps>k*L^2/(2*0.9^2)≈4.94，取 eps=10；
     * (b) 缓弯圆配宽松 eps，同理。 */
    AdaptConfig loose_nd = { 10.0 };
    AdaptiveTable t;
    int rc = adapt_build(pts[3], NP, &loose_nd, &t);
    printf("[拒绝用例a] 近退化 eps=10 rc=%d（-2=间隔大于总长拒绝）\n", rc);
    AdaptConfig loose_circle = { 200.0 };
    rc = adapt_build(pts[0], NP, &loose_circle, &t);
    printf("[拒绝用例b] 缓弯 eps=200 rc=%d（-2=间隔大于总长拒绝）\n", rc);

    /* 错误用例一之极端：近退化配极小 eps，表长超硬上限，按参数报错（-1） */
    AdaptConfig extreme = { 1e-10 };
    rc = adapt_build(pts[3], NP, &extreme, &t);
    printf("[报错用例] 近退化 eps=1e-12 表长超上限 rc=%d（-1=参数不可满足）\n", rc);

    /* 错误用例二：点少于二 / eps 非正 */
    APoint one[1] = {{0, 0}};
    AdaptConfig cfg = { 0.01 };
    rc = adapt_build(one, 1, &cfg, &t);
    printf("[报错用例] 曲线点少于二 rc=%d（-1=参数报错）\n", rc);
    AdaptConfig bad = { 0.0 };
    rc = adapt_build(pts[0], NP, &bad, &t);
    printf("[报错用例] eps=0 rc=%d（-1=参数报错）\n", rc);
    return 0;
}
