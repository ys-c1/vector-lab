#include "stroke.h"
#include "adaptive.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#define PI 3.14159265358979323846

static const char *flag_name(int f) {
    return f == SCURV_ZERO ? "ZERO" : f == SCURV_INF ? "INF " : "ok  ";
}

static int run_class(const char *name, const SPoint *pts, int n, double target) {
    double widths[STROKE_MAX_SEG];
    SWidthReport rep;
    if (stroke_widths_solve(pts, n, target, widths, &rep) != 0) {
        printf("== %s == solve FAILED\n", name);
        return 1;
    }
    printf("== %s == target=%.3f segs=%d\n", name, target, rep.nseg);
    printf("seg  kappa         flag  width     dev\n");
    for (int i = 0; i < rep.nseg; ++i) {
        const SSegReport *s = &rep.seg[i];
        char devbuf[32];
        if (isnan(s->deviation)) snprintf(devbuf, sizeof devbuf, "n/a");
        else snprintf(devbuf, sizeof devbuf, "%+.4f%%", s->deviation * 100.0);
        printf("%-4d %-13.6e %s  %-9.6f %s\n", i, s->kappa, flag_name(s->flag),
               s->width, devbuf);
    }
    printf("min=%.6f max=%.6f ratio=%.6f maxdev=%.4f%% time=%.1fus\n",
           rep.min_width, rep.max_width, rep.max_width / rep.min_width,
           rep.max_abs_deviation * 100.0, rep.seconds * 1e6);
    if (rep.max_width / rep.min_width > 1.2 + 1e-12) {
        printf("RATIO LIMIT EXCEEDED\n");
        return 1;
    }
    /* 逐段落盘 */
    char path[64];
    snprintf(path, sizeof path, "widths_%s.txt", name);
    FILE *f = fopen(path, "w");
    if (!f) return 1;
    for (int i = 0; i < rep.nseg; ++i)
        fprintf(f, "%d %.17g %d %.17g\n", i, rep.seg[i].kappa, rep.seg[i].flag,
                rep.seg[i].width);
    fclose(f);
    printf("wrote %s (%d segs)\n\n", path, rep.nseg);
    return 0;
}

int main(void) {
    int rc = 0;
    const double T = 2.0;

    /* 直线 */
    SPoint line[4] = {{0,0},{10,0},{20,0},{30,0}};
    /* 缓弯：R=100 圆弧 */
    SPoint gentle[8];
    for (int i = 0; i < 8; ++i) {
        double a = i * 6.0 * PI / 180.0;
        gentle[i].x = 100.0 * cos(a); gentle[i].y = 100.0 * sin(a);
    }
    /* 急弯：R=3 圆弧 */
    SPoint sharp[9];
    for (int i = 0; i < 9; ++i) {
        double a = i * 20.0 * PI / 180.0;
        sharp[i].x = 3.0 * cos(a); sharp[i].y = 3.0 * sin(a);
    }
    /* 回折：发卡弯 + 末尾 180° 原路折返 */
    SPoint fold[8] = {{0,0},{16,0},{16,0.25},{2,0.25},{2,0},{-6,0},{-2,0},{-8,0}};

    rc |= run_class("straight", line, 4, T);
    rc |= run_class("gentle", gentle, 8, T);
    rc |= run_class("sharp", sharp, 9, T);
    rc |= run_class("foldback", fold, 8, T);

    /* 负宽拒绝：指出段号 */
    double wneg[3] = {2.0, -0.5, 2.0};
    int err_seg = -1;
    SPoly out;
    int r = stroke_polyline_varying(line, 4, wneg, &out, &err_seg);
    printf("negative width: rc=%d seg=%d (expect rc=-3 seg=1)\n\n", r, err_seg);
    if (r != -3 || err_seg != 1) rc = 1;

    /* 关掉新逻辑：等宽路径逐字节不变 */
    SPoint demo[4] = {{0,0},{30,0},{30,20},{0,20}};
    SPoly a, b;
    stroke_polyline(demo, 4, 3.0, &a);
    stroke_outline(demo, 4, 3.0, 0, &b, NULL);
    int same = a.count == b.count &&
               memcmp(a.pts, b.pts, sizeof(SPoint) * (size_t)a.count) == 0;
    printf("uniform path (adaptive off) byte-identical: %s\n",
           same ? "OK" : "MISMATCH");
    if (!same) rc = 1;
    printf("outline %d\n", a.count);
    for (int i = 0; i < a.count; ++i) printf("  %.2f %.2f\n", a.pts[i].x, a.pts[i].y);
    return rc;
}
