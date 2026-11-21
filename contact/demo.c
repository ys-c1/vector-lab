#define _POSIX_C_SOURCE 199309L
#include "contact.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

/* 基准曲线控制点 (0,0),(0,3),(3,1),(3,0)：
 *   x(t) = 9t^2 - 6t^3,  y(t) = 9t - 15t^2 + 6t^3；
 *   y'(t) = 9 - 30t + 18t^2 = 0  ->  t* = (5 - sqrt(7)) / 6。 */
static CtPoint kCurve[4] = {{0,0},{0,3},{3,1},{3,0}};

static double tstar(void) { return (5.0 - sqrt(7.0)) / 6.0; }
static double curve_y(double t) { return 9.0*t - 15.0*t*t + 6.0*t*t*t; }
static double curve_x(double t) { return 9.0*t*t - 6.0*t*t*t; }

/* 按公式独立手算某区间的判别式：用解析式 x(t)/y(t)（而不是 Bernstein 求值）
 * 取三点，再用 long double 按 A/B/C 公式算 D。与库内实现属于两条不同的
 * 计算路径，差异只应来自浮点舍入。 */
static double hand_disc(CtPoint b0, CtPoint b1, int spans, int span) {
    long double sx = (long double)(b1.x - b0.x);
    long double sy = (long double)(b1.y - b0.y);
    long double L = sqrtl(sx*sx + sy*sy);
    long double ta = (long double)span/(long double)spans;
    long double tb = (long double)(span+1)/(long double)spans;
    long double tm = 0.5L*(ta + tb);
    /* 解析式：x(t)=9t^2-6t^3, y(t)=9t-15t^2+6t^3 */
    long double h[3];
    long double tt[3] = {ta, tm, tb};
    for (int k = 0; k < 3; ++k) {
        long double x = 9.0L*tt[k]*tt[k] - 6.0L*tt[k]*tt[k]*tt[k];
        long double y = 9.0L*tt[k] - 15.0L*tt[k]*tt[k] + 6.0L*tt[k]*tt[k]*tt[k];
        long double px = x - (long double)b0.x;
        long double py = y - (long double)b0.y;
        h[k] = (sx*py - sy*px) / L;
    }
    long double A = 2.0L*(h[0] - 2.0L*h[1] + h[2]);
    long double B = 4.0L*h[1] - 3.0L*h[0] - h[2];
    long double C = h[0];
    return (double)(B*B - 4.0L*A*C);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

typedef struct {
    const char *name;
    CtPoint b0, b1;
    int expect_tangent;     /* 1 = 本场景应报擦边 */
    double tangent_t;       /* 解析切点参数（expect_tangent 时有效） */
} Scenario;

int main(void) {
    double ts = tstar(), ys = curve_y(ts), xs = curve_x(ts);
    printf("基准曲线控制点 (0,0) (0,3) (3,1) (3,0)\n");
    printf("解析极值 t* = (5-sqrt(7))/6 = %.15f\n", ts);
    printf("切点 (% .12f, % .12f)\n\n", xs, ys);

    Scenario sc[] = {
        {"解析切线 y=y*（相切）",      {-1, ys},       {3, ys},       1, 0},
        {"切线略上方 +0.01（无接触）", {-1, ys+0.01},  {3, ys+0.01},  0, 0},
        {"切线略下方 -0.01（穿过两次）",{-1, ys-0.01}, {3, ys-0.01},  0, 0},
        {"y=1（穿过两个交点）",        {-1, 1},        {3, 1},        0, 0},
        {"曲线端点 t=0（擦边）",       {-1, 0},        {1, 0},        1, 0},
        {"共线贴合 y=0",               {-1, 0},        {4, 0},        0, 0},
    };
    const int NS = (int)(sizeof sc / sizeof sc[0]);
    int spans_opt[] = {8, 64};

    printf("%-26s %5s %6s %5s %5s %8s %13s %13s %11s\n",
           "场景", "段数", "穿过", "擦边", "判别负", "拒绝", "相切t误差",
           "手算D最大差", "耗时/千次(ms)");
    int bad = 0;

    for (int s = 0; s < NS; ++s) {
        for (int k = 0; k < 2; ++k) {
            int spans = spans_opt[k];
            CtScan scan;
            char ef[CT_FIELD_MAX];
            int rc = ct_contact(kCurve, sc[s].b0, sc[s].b1, spans,
                                CT_F_CLASSIFY, 1e-3, &scan, ef);
            if (rc != CT_OK) {
                printf("%-26s %5d  rc=%d field=%s\n", sc[s].name, spans, rc, ef);
                bad = 1;
                continue;
            }

            /* 相切 t 误差：仅在该场景确实报出擦边时计算 */
            double terr = NAN;
            if (sc[s].expect_tangent) {
                for (int i = 0; i < scan.count; ++i) {
                    if (scan.hits[i].kind == CT_KIND_GRAZE) {
                        terr = fabs(scan.hits[i].t - (s == 0 ? ts : 0.0));
                        break;
                    }
                }
                if (isnan(terr)) { printf("  预期擦边但未报出\n"); bad = 1; }
            }

            /* 与按公式手算的判别式逐行核对 */
            double dmax = 0.0;
            for (int i = 0; i < scan.nrows; ++i) {
                double D = hand_disc(sc[s].b0, sc[s].b1, spans, i);
                double diff = fabs(scan.rows[i].disc - D);
                if (diff > dmax) dmax = diff;
            }

            /* 耗时：分类路径重复 1000 次 */
            const int reps = 1000;
            double t0 = now_ms();
            for (int r = 0; r < reps; ++r) {
                CtScan tmp;
                (void)ct_contact(kCurve, sc[s].b0, sc[s].b1, spans,
                                 CT_F_CLASSIFY, 1e-3, &tmp, ef);
            }
            double ms = (now_ms() - t0) / reps * 1000.0;

            char terr_s[32], dmax_s[32];
            if (isnan(terr)) strcpy(terr_s, "-");
            else snprintf(terr_s, sizeof terr_s, "%.3e", terr);
            snprintf(dmax_s, sizeof dmax_s, "%.3e", dmax);

            printf("%-26s %5d %6d %5d %5d %8d %13s %13s %11.3f\n",
                   sc[s].name, spans, scan.crosses, scan.grazes,
                   scan.disc_neg, scan.rejects, terr_s, dmax_s, ms);
        }
    }

    /* 导出接触分类表（8 与 64 段各一份），并对 64 段的多场景另导出接触点表。 */
    char paths[2][64];
    for (int k = 0; k < 2; ++k) {
        int spans = spans_opt[k];
        CtScan scan;
        char ef[CT_FIELD_MAX];
        if (ct_contact(kCurve, sc[0].b0, sc[0].b1, spans, CT_F_CLASSIFY, 1e-3,
                       &scan, ef) != CT_OK) {
            fprintf(stderr, "scan failed: %s\n", ef);
            return 1;
        }
        snprintf(paths[k], sizeof paths[k], "build/contact_rows_%d.csv", spans);
        FILE *fp = fopen(paths[k], "w");
        if (!fp) { perror(paths[k]); return 1; }
        if (ct_export_rows(&scan, fp) != CT_OK) { fclose(fp); return 1; }
        fclose(fp);

        char hits_path[64];
        snprintf(hits_path, sizeof hits_path, "build/contact_hits_%d.csv", spans);
        fp = fopen(hits_path, "w");
        if (!fp) { perror(hits_path); return 1; }
        if (ct_export_hits(&scan, fp) != CT_OK) { fclose(fp); return 1; }
        fclose(fp);
    }
    printf("\n分类表已导出：%s（%d 段）、%s（%d 段）\n",
           paths[0], spans_opt[0], paths[1], spans_opt[1]);
    printf("切点表已导出：build/contact_hits_8.csv / build/contact_hits_64.csv\n");

    return bad ? 1 : 0;
}
