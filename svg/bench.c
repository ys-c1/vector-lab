/* 平滑段（S/T）反射控制点基准：与逐段手写控制点表比对，量接缝处
   切线角误差并给上界，按两档细分段数量接缝数与耗时。
   列：接缝号 | 切线角deg | 段长(入/出) | 切线角误差deg | 误差上界deg | 耗时us */
#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define PI 3.14159265358979323846

/* 手写控制点表：每段曲线的显式控制点。S/T 行的首控制点即手工算出的
   反射点，用来和解析器的隐含反射逐点比对。 */
typedef struct { char kind; double x1, y1, x2, y2, x, y; } Row;
static const Row rows[] = {
    {'C', 140, 20, 180, 60, 180, 100},
    {'C', 180, 140, 135, 175, 100, 180}, /* S: c1 = 2*(180,100)-(180,60) */
    {'C', 65, 185, 20, 140, 20, 100},    /* S: c1 = 2*(100,180)-(135,175) */
    {'C', 20, 60, 60, 20, 100, 20},      /* S: c1 = 2*(20,100)-(20,140) */
    {'Q', 70, 120, 0, 0, 100, 150},
    {'Q', 130, 180, 0, 0, 165, 145},     /* T: q1 = 2*(100,150)-(70,120) */
    {'Q', 200, 110, 0, 0, 220, 150},     /* T: q1 = 2*(165,145)-(130,180) */
};
#define NROWS ((int)(sizeof rows / sizeof rows[0]))

/* 接缝 k 由 rows[seam_rows[k][0]] 与 rows[seam_rows[k][1]] 相接 */
static const int seam_rows[][2] = {{0, 1}, {1, 2}, {2, 3}, {4, 5}, {5, 6}};
#define NSEAMS ((int)(sizeof seam_rows / sizeof seam_rows[0]))

static const char *D_SMOOTH =
    "M100,20 C140,20 180,60 180,100 S135,175 100,180 S20,140 20,100 S60,20 100,20 Z"
    " M40,150 Q70,120 100,150 T165,145 T220,150";
static const char *D_EXPLICIT =
    "M100,20 C140,20 180,60 180,100 C180,140 135,175 100,180 C65,185 20,140 20,100"
    " C20,60 60,20 100,20 Z"
    " M40,150 Q70,120 100,150 Q130,180 165,145 Q200,110 220,150";

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec * 1e-3;
}

static double seg_len(VSeg s) {
    return hypot(s.b.x - s.a.x, s.b.y - s.a.y);
}

static double seg_angle(VSeg s) {
    return atan2(s.b.y - s.a.y, s.b.x - s.a.x) * 180.0 / PI;
}

static double angle_diff(double a, double b) {
    double d = fabs(a - b);
    while (d > 180.0) d = fabs(d - 360.0);
    return d;
}

/* 行的完整控制点（P0 为上一行终点或子路径起点）；二次行只用 c[0..2] */
static int row_points(int r, VPoint c[4]) {
    static const VPoint subpath_start[] = {{100, 20}, {40, 150}};
    VPoint p0 = (r < 4) ? subpath_start[0] : subpath_start[1];
    if (r > 0 && r != 4) { p0.x = rows[r - 1].x; p0.y = rows[r - 1].y; }
    int is_cubic = rows[r].kind == 'C';
    c[0] = p0;
    c[1].x = rows[r].x1; c[1].y = rows[r].y1;
    if (is_cubic) { c[2].x = rows[r].x2; c[2].y = rows[r].y2; c[3].x = rows[r].x; c[3].y = rows[r].y; }
    else { c[2].x = rows[r].x; c[2].y = rows[r].y; }
    return is_cubic;
}

static VPoint deriv(const VPoint c[4], int is_cubic, double t) {
    VPoint d;
    double u = 1.0 - t;
    if (is_cubic) {
        d.x = 3 * u * u * (c[1].x - c[0].x) + 6 * u * t * (c[2].x - c[1].x)
            + 3 * t * t * (c[3].x - c[2].x);
        d.y = 3 * u * u * (c[1].y - c[0].y) + 6 * u * t * (c[2].y - c[1].y)
            + 3 * t * t * (c[3].y - c[2].y);
    } else {
        d.x = 2 * u * (c[1].x - c[0].x) + 2 * t * (c[2].x - c[1].x);
        d.y = 2 * u * (c[1].y - c[0].y) + 2 * t * (c[2].y - c[1].y);
    }
    return d;
}

/* 端部一个细分段内切线角相对端点切线角的最大偏转（65 点采样） */
static double end_turn(int r, int subdiv, int at_end) {
    VPoint c[4];
    int is_cubic = row_points(r, c);
    double t_end = at_end ? 1.0 : 0.0;
    VPoint de = deriv(c, is_cubic, t_end);
    double a_end = atan2(de.y, de.x) * 180.0 / PI;
    double t0 = at_end ? 1.0 - 1.0 / subdiv : 0.0;
    double t1 = at_end ? 1.0 : 1.0 / subdiv;
    double worst = 0.0;
    for (int i = 0; i <= 64; ++i) {
        double t = t0 + (t1 - t0) * i / 64.0;
        VPoint d = deriv(c, is_cubic, t);
        double a = atan2(d.y, d.x) * 180.0 / PI;
        double dev = angle_diff(a, a_end);
        if (dev > worst) worst = dev;
    }
    return worst;
}

int main(void) {
    const int tiers[] = {8, 32};
    const int w = 240, h = 200;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) return 1;
    printf("接缝号 | 切线角deg | 段长(入/出) | 切线角误差deg | 误差上界deg | 耗时us\n");
    for (int ti = 0; ti < 2; ++ti) {
        int subdiv = tiers[ti];
        VPath a, b; VSeams sm;
        /* 与逐段手写控制点表比对：反射展开必须与显式 C/Q 逐点一致 */
        if (svg_parse2(D_SMOOTH, &a, subdiv, 0, &sm) != 0) return 1;
        if (svg_parse2(D_EXPLICIT, &b, subdiv, 0, 0) != 0) return 1;
        if (a.count != b.count) return 1;
        for (int i = 0; i < a.count; ++i)
            if (a.segs[i].a.x != b.segs[i].a.x || a.segs[i].a.y != b.segs[i].a.y
             || a.segs[i].b.x != b.segs[i].b.x || a.segs[i].b.y != b.segs[i].b.y) {
                printf("MISMATCH tier %d seg %d\n", subdiv, i);
                return 1;
            }
        if (sm.count != NSEAMS) return 1;
        /* 耗时：细分+解析 5000 次，填图 50 次，取均值 */
        double t0 = now_us();
        for (int r = 0; r < 5000; ++r)
            if (svg_parse2(D_SMOOTH, &a, subdiv, 0, &sm) != 0) return 1;
        double t_parse = (now_us() - t0) / 5000.0;
        t0 = now_us();
        for (int r = 0; r < 50; ++r)
            if (svg_fill(&a, w, h, buf) != 0) return 1;
        double t_fill = (now_us() - t0) / 50.0;
        double t_all = t_parse + t_fill;
        printf("-- 细分段数 %d：总段数 %d，接缝数 %d，解析 %.2f us，填图 %.2f us\n",
               subdiv, a.count, sm.count, t_parse, t_fill);
        for (int k = 0; k < sm.count; ++k) {
            int j = sm.at[k];
            int r0 = seam_rows[k][0], r1 = seam_rows[k][1];
            VPoint c[4];
            int is_cubic = row_points(r0, c);
            VPoint tan;
            if (is_cubic) { tan.x = c[3].x - c[2].x; tan.y = c[3].y - c[2].y; }
            else { tan.x = c[2].x - c[1].x; tan.y = c[2].y - c[1].y; }
            double ang = atan2(tan.y, tan.x) * 180.0 / PI;
            double err = angle_diff(seg_angle(a.segs[j - 1]), seg_angle(a.segs[j]));
            /* 上界：弦向被端部细分段切线锥包住，两侧偏转相加，乘 1.5 安全系数 */
            double bound = 1.5 * (end_turn(r0, subdiv, 1) + end_turn(r1, subdiv, 0));
            printf("%d | %.2f | %.3f/%.3f | %.4f | %.4f | %.2f\n",
                   k, ang, seg_len(a.segs[j - 1]), seg_len(a.segs[j]),
                   err, bound, t_all);
            if (err > bound) { printf("BOUND EXCEEDED at seam %d\n", k); return 1; }
        }
    }
    free(buf);
    return 0;
}
