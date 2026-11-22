#define _POSIX_C_SOURCE 199309L
#include "fit.h"
#include <stdio.h>
#include <math.h>
#include <time.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 演示场景：四组点集（生成时统一旋转 30 度，模拟“转一下再进画布”）。 */
static FPoint rect4[4];          /* 长矩形 20x4：改善显著 */
static FPoint diamond4[4];       /* 菱形（正方形转 45 度）：改善显著 */
static FPoint lshape8[8];        /* L 形：倾斜后需转回，改善显著 */
static FPoint square4[4];        /* 轴对齐正方形：无差别场景，无角可改善零度 */

static FPoint rotate_pt(FPoint p, double a) {
    double c = cos(a), s = sin(a);
    FPoint r = { p.x * c - p.y * s, p.x * s + p.y * c };
    return r;
}

static void build_scenes(void) {
    double tilt = M_PI / 6.0;
    FPoint rb[4] = {{-10,-2},{10,-2},{10,2},{-10,2}};
    FPoint db[4] = {{0,1},{1,0},{0,-1},{-1,0}};
    FPoint sb[4] = {{0,0},{8,0},{8,8},{0,8}};
    FPoint lb2[6] = {{0,0},{4,0},{4,1},{1,1},{1,4},{0,4}};
    for (int i = 0; i < 4; ++i) rect4[i] = rotate_pt(rb[i], tilt);
    for (int i = 0; i < 4; ++i) diamond4[i] = rotate_pt(db[i], tilt);
    for (int i = 0; i < 6; ++i) lshape8[i] = rotate_pt(lb2[i], tilt);
    for (int i = 0; i < 4; ++i) square4[i] = sb[i];   /* 不倾斜：展示无差别场景 */
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void report(const char *name, const FPoint *pts, int n, int reps) {
    FitResult r;
    double t0 = now_sec();
    for (int i = 0; i < reps; ++i) fit_optimize(pts, n, &r);
    double dt = (now_sec() - t0) * 1e6 / reps;

    double cut = (1.0 - r.area / r.area0) * 100.0;
    printf("%-8s angle=%.6f rad (%7.3f deg)  area=%.4f  area0=%.4f  "
           "drop=%.2f%%  time=%.3f us  %s\n",
           name, r.angle, r.angle * 180.0 / M_PI, r.area, r.area0, cut, dt,
           r.indifferent ? "INDIFERENT (正方形类无差别)" :
           (r.area < r.area0 * 0.9 ? "OK(>10%)" : "FAIL(<10%)"));
}

int main(void) {
    build_scenes();
    int reps = 100000;
    printf("每组场景 fit_optimize 重复 %d 次取平均耗时\n", reps);
    report("rect",    rect4,    4, reps);
    report("diamond", diamond4, 4, reps);
    report("lshape",  lshape8, 6, reps);
    report("square",  square4,  4, reps);
    return 0;
}
