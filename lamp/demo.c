#define _POSIX_C_SOURCE 200809L
#include "lamp.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*
 * 基准：用一段带转折的闭合感折线（正弦扰动）分别取 16 段与 128 段，
 * 在相同目标点距下记录点数、耗时、点距误差，并与按总长手算的
 * 均匀分布核对。点距表可导出 CSV：
 *   ./demo [csv 路径]   默认 lamp_spacing.csv
 */
#define N_MAX 129
#define SPACING 7.5

static double poly_len(const LPoint *p, int n) {
    double s = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        s += sqrt(dx*dx + dy*dy);
    }
    return s;
}

static double arc_of(const LPoint *p, int n, LPoint q) {
    double acc = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-12) continue;
        double t = ((q.x - p[i].x) * dx + (q.y - p[i].y) * dy) / (len * len);
        if (t < 0) t = 0; else if (t > 1) t = 1;
        /* 残差判定是否真在段上；顶点(t≈0)留给上一段 t≈1，避免弧长回退 */
        double rx = q.x - (p[i].x + dx * t);
        double ry = q.y - (p[i].y + dy * t);
        int on = (rx*rx + ry*ry) < 1e-12;
        if (on && (t > 1e-9 || i == 0)) return acc + t * len;
        acc += len;
    }
    return acc;
}

static void build_curve(LPoint *p, int segs) {
    for (int i = 0; i <= segs; ++i) {
        double u = (double)i / segs;              /* 0..1 */
        double ang = u * 2.0 * M_PI;
        double r = 80.0 + 18.0 * sin(3.0 * ang); /* 半径带扰动，制造转折 */
        p[i].x = 100.0 + r * cos(ang);
        p[i].y = 100.0 + r * sin(ang);
    }
}

static void bench(int segs, const LPoint *p) {
    int n = segs + 1;
    double total = poly_len(p, n);
    LLamps lamps;

    /* 重复放置取平均耗时，避免单次亚微秒调用被计时器噪声淹没 */
    enum { REPS = 2000 };
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = LAMP_OK;
    for (int r = 0; r < REPS; ++r)
        rc = lamp_place(p, n, SPACING, 1, &lamps);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = ((t1.tv_sec - t0.tv_sec) * 1e3
                 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / REPS;
    if (rc != LAMP_OK) { printf("bench %d segs failed: %s\n", segs, lamp_strerror(rc)); return; }

    /* 手算均匀分布：s = d, 2d, ...（与库内放置规则一致）*/
    int hand = 0;
    for (double s = SPACING; s < total - 1e-12; s += SPACING) ++hand;

    double max_err = 0, sum_gap = 0, min_gap = 1e9, max_gap = 0, prev = 0;
    for (int k = 0; k < lamps.count; ++k) {
        double s = arc_of(p, n, lamps.pts[k]);
        double want = (k + 1) * SPACING;
        double e = fabs(s - want);
        if (e > max_err) max_err = e;
        double gap = s - prev;
        if (gap < min_gap) min_gap = gap;
        if (gap > max_gap) max_gap = gap;
        sum_gap += gap;
        prev = s;
    }
    printf("== %d 段（%d 顶点）==\n", segs, n);
    printf("  总长 %.4f，目标点距 %.2f\n", total, SPACING);
    printf("  点数 %d，手算均匀分布点数 %d，%s\n",
           lamps.count, hand, lamps.count == hand ? "一致" : "不一致");
    printf("  耗时 %.4f ms\n", ms);
    printf("  点距误差(max vs 手算) %.3e；实际点距 min %.6f / mean %.6f / max %.6f\n",
           max_err, min_gap, sum_gap / lamps.count, max_gap);
}

int main(int argc, char **argv) {
    const char *csv = argc > 1 ? argv[1] : "lamp_spacing.csv";
    LPoint p[N_MAX];

    build_curve(p, 16);
    bench(16, p);
    build_curve(p, 128);
    bench(128, p);

    /* 用 128 档导出一份完整点距表 */
    build_curve(p, 128);
    LLamps lamps;
    int rc = lamp_place(p, 129, SPACING, 1, &lamps);
    if (rc != LAMP_OK) { printf("export failed: %s\n", lamp_strerror(rc)); return 1; }
    rc = lamp_export_csv(&lamps, p, 129, csv);
    if (rc != LAMP_OK) { printf("export failed: %s\n", lamp_strerror(rc)); return 1; }
    printf("点距表已导出 %s（%d 行）\n", csv, lamps.count);
    return 0;
}
