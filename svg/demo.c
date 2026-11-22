/* svg 模块演示。
 *
 *   1. 基准路径保持原样：纯线段，开关为假，输出 demo.pgm 与改动前逐字节相同。
 *   2. 椭圆弧参数表：逐段列出圆心、生效半径、起始角 θ1、扫过角 Δθ、
 *      两档弦数、径向偏差上界与手算实测偏差。
 *   3. 两档对比：整圆兼容（开关假）、椭圆余量档、椭圆紧凑档的像素数与耗时。
 *   4. 错误表：半径不足 / 起止点重合 / 旋转角越界，给出段号与字段名。
 */
#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

static int count_on(const unsigned char *b, int n) {
    int on = 0;
    for (int i = 0; i < n; ++i) if (b[i]) ++on;
    return on;
}

/* 手算：把点代入椭圆方程求径向偏差。
 * 先把点搬到椭圆局部坐标 (u,v)，该方向的椭圆半径由
 *   1/r(θ)^2 = cos^2θ/rx^2 + sin^2θ/ry^2
 * 给出，偏差 = | |(u,v)| - r(θ) |。与库内折线无关。 */
static double hand_dev(const SvgArcFrame *f, double x, double y) {
    double dx = x - f->center.x, dy = y - f->center.y;
    double c = cos(f->xrot), sn = sin(f->xrot);
    double u =  c * dx + sn * dy;
    double v = -sn * dx + c * dy;
    double a = atan2(v, u);
    double r = 1.0 / sqrt(cos(a) * cos(a) / (f->rx * f->rx)
                        + sin(a) * sin(a) / (f->ry * f->ry));
    return fabs(sqrt(u * u + v * v) - r);
}

/* 由参数式自己生成弦端点：不经过库内折线函数。 */
static VPoint param_point(const SvgArcFrame *f, double t) {
    double c = cos(f->xrot), sn = sin(f->xrot);
    double u = f->rx * cos(t), v = f->ry * sin(t);
    return (VPoint){f->center.x + c * u - sn * v, f->center.y + sn * u + c * v};
}

/* 两档弦数下，逐弦取 15 个内点算实测径向偏差。 */
static double measured_dev(const VSeg *s, const SvgArcFrame *f, int n) {
    double worst = 0.0;
    for (int k = 0; k < n; ++k) {
        VPoint p0 = param_point(f, f->theta1 + f->dtheta * (double)k / (double)n);
        VPoint p1 = param_point(f, f->theta1 + f->dtheta * (double)(k + 1) / (double)n);
        for (int j = 1; j < 16; ++j) {
            double u = (double)j / 16.0;
            double x = p0.x + (p1.x - p0.x) * u;
            double y = p0.y + (p1.y - p0.y) * u;
            double d = hand_dev(f, x, y);
            if (d > worst) worst = d;
        }
    }
    (void)s;
    return worst;
}

static double median_time(int mode, const VPath *p, int w, int h,
                          unsigned char *buf, int reps) {
    double *t = malloc(sizeof(double) * (size_t)reps);
    for (int r = 0; r < reps; ++r) {
        double t0 = now_us();
        svg_fill_ex(p, w, h, buf, (unsigned)mode, 1e-3, NULL);
        t[r] = now_us() - t0;
    }
    for (int i = 1; i < reps; ++i)
        for (int j = i; j > 0 && t[j] < t[j - 1]; --j) {
            double tmp = t[j]; t[j] = t[j - 1]; t[j - 1] = tmp;
        }
    double m = t[reps / 2];
    free(t);
    return m;
}

int main(void) {
    /* ---- 1. 基准路径：与原 demo 完全相同的调用 ---- */
    VPath base;
    if (svg_parse("M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z", &base) != 0) return 1;
    int bw = 50, bh = 50;
    unsigned char *bbuf = malloc((size_t)bw * bh);
    if (svg_fill(&base, bw, bh, bbuf) != 0) return 1;
    if (svg_save_pgm("demo.pgm", bbuf, bw, bh) != 0) return 1;
    printf("segs %d on %d\n", base.count, count_on(bbuf, bw * bh));
    free(bbuf);

    /* ---- 2. 椭圆弧参数表 ---- */
    struct Case {
        const char *name;
        const char *d;
        int w, h;
    } cases[] = {
        {"半圆 rx=5",       "M0,0 A5,5 0 0 1 10,0", 12, 12},
        {"椭圆 30° 小弧",   "M2,20 A20,10 30 0 1 30,20 L30,30 Z", 48, 48},
        {"椭圆 30° 大弧",   "M2,20 A20,10 30 1 0 30,20 L30,30 Z", 48, 48},
        {"竖长椭圆 15°",    "M1,1 A8,80 15 0 1 11,11 L4,20 Z", 60, 40},
        {"极扁椭圆 -60°",   "M0,0 A40,5 -60 1 0 10,0 L10,10 Z", 48, 24},
    };
    const double tol = 1e-3;

    printf("\n椭圆弧参数表（tol=%.0e，角度单位：度；耗时为细分一次的中位数）\n", tol);
    printf("%-15s %3s %10s %10s %9s %9s %9s %9s %5s %5s %10s %10s %9s %9s\n",
           "路径", "段", "圆心x", "圆心y", "θ1", "Δθ", "rx", "ry",
           "n+", "n-", "上界", "实测偏差", "端点残差", "细分(us)");
    int bad = 0;
    for (unsigned i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        VPath p;
        if (svg_parse_flags(cases[i].d, &p, SVG_F_ARCS) != SVG_OK) {
            printf("%-15s 解析失败: %s\n", cases[i].name,
                   svg_last_error_detail()->msg);
            bad = 1;
            continue;
        }
        for (int k = 0; k < p.count; ++k) {
            if (p.segs[k].kind != SV_ARC) continue;
            SvgArcFrame f;
            int n, np, nm;
            double bound;
            int frc = svg_arc_frame(k, &p.segs[k], &f);
            int rc = frc == SVG_OK
                   ? svg_arc_chords(k, &p.segs[k], tol, &n, &bound, &np, &nm)
                   : frc;
            if (rc != SVG_OK) {
                printf("%-15s %3d 失败: %s\n", cases[i].name, k,
                       svg_last_error_detail()->msg);
                bad = 1;
                continue;
            }
            double dvp = measured_dev(&p.segs[k], &f, np);
            double dvm = measured_dev(&p.segs[k], &f, nm);
            double h1 = hand_dev(&f, param_point(&f, f.theta1).x,
                                 param_point(&f, f.theta1).y);
            double h2 = hand_dev(&f, param_point(&f, f.theta1 + f.dtheta).x,
                                 param_point(&f, f.theta1 + f.dtheta).y);
            /* 上界按两档分别算：n 越小上界越大 */
            double bm = bound * (double)(np * np) / (double)(nm * nm);
            /* 细分耗时：svg_arc_polyline 单次中位数 */
            VPoint pts[SVG_ARC_CHORD_CAP + 1];
            const int reps = 200;
            double *tt = malloc(sizeof(double) * (size_t)reps);
            for (int r = 0; r < reps; ++r) {
                double t0 = now_us();
                svg_arc_polyline(k, &p.segs[k], tol, pts, SVG_ARC_CHORD_CAP + 1, NULL, NULL);
                tt[r] = now_us() - t0;
            }
            for (int a = 1; a < reps; ++a)
                for (int b = a; b > 0 && tt[b] < tt[b - 1]; --b) {
                    double tmp = tt[b]; tt[b] = tt[b - 1]; tt[b - 1] = tmp;
                }
            double sub_us = tt[reps / 2];
            free(tt);
            printf("%-15s %3d %10.4f %10.4f %9.3f %9.3f %9.4f %9.4f %5d %5d %10.3e %10.3e %9.1e %9.3f\n",
                   cases[i].name, k, f.center.x, f.center.y,
                   f.theta1 * 180.0 / PI, f.dtheta * 180.0 / PI, f.rx, f.ry,
                   np, nm, bound, dvp, h1 > h2 ? h1 : h2, sub_us);
            if (dvp > bound * (1 + 1e-9) + 1e-15) bad = 1;
            if (dvm > bm * (1 + 1e-9) + 1e-15) bad = 1;
            if (dvm > bound) {
                printf("%-15s %3s 紧凑档实测 %.3e 超出余量档上界 %.3e（符合预期）\n",
                       "", "", dvm, bound);
            }
        }
    }

    /* ---- 3. 两档对比：像素数与耗时 ---- */
    printf("\n两档椭圆与整圆兼容（%s，tol=%.0e）\n", cases[1].d, tol);
    int w = cases[1].w, h = cases[1].h;
    unsigned char *buf = malloc((size_t)w * h);
    VPath p;
    if (svg_parse_flags(cases[1].d, &p, SVG_F_ARCS) != SVG_OK) return 1;

    struct Mode { const char *name; unsigned mode; } modes[] = {
        {"整圆(开关假)", SVG_ARC_CIRCLE},
        {"椭圆-余量档",  SVG_ARC_ELLIPSE},
        {"椭圆-紧凑档",  SVG_ARC_ELLIPSE_MIN},
    };
    const int reps = 200;
    int on_circle = 0;
    printf("%-14s %8s %8s %12s %12s\n", "模式", "弦数", "像素数", "相对整圆", "耗时/次(us)");
    for (unsigned m = 0; m < sizeof modes / sizeof modes[0]; ++m) {
        SvgFillStats st;
        if (svg_fill_ex(&p, w, h, buf, modes[m].mode, tol, &st) != SVG_OK) {
            printf("%-14s 填充失败: %s\n", modes[m].name, svg_last_error_detail()->msg);
            return 1;
        }
        int on = count_on(buf, w * h);
        if (m == 0) on_circle = on;
        double ms = median_time((int)modes[m].mode, &p, w, h, buf, reps);
        printf("%-14s %8ld %8d %11.3f%% %12.2f\n", modes[m].name, st.chords, on,
               on_circle ? 100.0 * (double)(on - on_circle) / (double)on_circle : 0.0,
               ms);
        char path[64];
        snprintf(path, sizeof path, "demo_%s.pgm",
                 m == 0 ? "circle" : (m == 1 ? "ellipse_plus" : "ellipse_min"));
        svg_save_pgm(path, buf, w, h);
    }

    /* 开关为假：svg_fill 与显式兼容模式逐字节一致 */
    unsigned char *a = malloc((size_t)w * h), *b = malloc((size_t)w * h);
    svg_fill(&p, w, h, a);
    svg_fill_ex(&p, w, h, b, SVG_ARC_CIRCLE, tol, NULL);
    printf("开关为假时 svg_fill 与兼容模式逐字节%s\n",
           memcmp(a, b, (size_t)w * h) == 0 ? "一致" : "不一致");
    if (memcmp(a, b, (size_t)w * h) != 0) bad = 1;
    free(a); free(b); free(buf);

    /* ---- 4. 错误表 ---- */
    struct Err {
        const char *name;
        const char *d;
        int mode;        /* 1 = 只解析，2 = 椭圆填充 */
    } errs[] = {
        {"半径不足", "M0,0 L2,2 A1,1 0 0 1 12,2", 2},
        {"起止点重合", "M3,3 L4,4 A5,2 10 1 1 4,4", 2},
        {"旋转角越界", "M0,0 L1,1 A5,2 -100 0 1 10,0", 2},
        {"rx 为零", "M0,0 A0,3 0 0 1 10,0", 2},
        {"非有限值", "M0,0 A5,5 0 0 1 inf,0", 1},
    };
    printf("\n错误表\n");
    printf("%-12s %-4s %-6s %-10s %-12s %s\n", "情形", "段号", "返回码", "字段", "数值", "说明");
    for (unsigned i = 0; i < sizeof errs / sizeof errs[0]; ++i) {
        VPath ep;
        int rc;
        if (errs[i].mode == 1) {
            rc = svg_parse_flags(errs[i].d, &ep, SVG_F_ARCS);
        } else {
            rc = svg_parse_flags(errs[i].d, &ep, SVG_F_ARCS);
            if (rc == SVG_OK) {
                unsigned char tiny[32 * 32];
                rc = svg_fill_ex(&ep, 32, 32, tiny, SVG_ARC_ELLIPSE, tol, NULL);
            }
        }
        const SvgError *e = svg_last_error_detail();
        printf("%-12s %-4d %-6d %-10s %-12.6g %s\n", errs[i].name, e->seg, rc,
               e->field, e->value, e->msg);
        if (rc >= 0) bad = 1;
        /* 段号：解析期或填充期都应给出具体段 */
        if (e->seg < 0) bad = 1;
    }

    printf("\n%s\n", bad ? "存在不符合预期的行" : "全部通过");
    return bad ? 1 : 0;
}
