/*
 * 自适应细分基准。
 *
 * 统一给定一个弦高误差上界 E，由它反解最大允许容差 T = 4E/3，
 * 四类曲线（缓弯、急弯、S 形、近直线）都按同一个 E 细分并逐段验证。
 * 每类报：反解容差、实际最大弦高误差、段数、细分耗时；任一段误差
 * 超过 E 就整类失败（非零退出码），并打印超界段号与参数区间。
 * 同一行再给出旧的固定 8 段在同一条曲线上的实际误差，作为“折线可见”
 * 的对照。
 *
 * 用法：./build/bench [E]   默认 E = 0.25
 */
#define _POSIX_C_SOURCE 199309L
#include "bezier.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define REPEAT 2000

static double ms_since(struct timespec t0) {
    struct timespec t1;
    clock_gettime(CLOCK_MONOTONIC, &t1);
    return (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
}

typedef struct {
    const char *name;
    BPoint c[4];
} Curve;

/* 把旧固定 8 段折线作为一个 BFlat 交给验证器，量出它的实际弦高误差。 */
static double legacy_fixed_error(const BPoint c[4], int samples) {
    BFlat f;
    BPathResult r;
    if (bezier_flatten_mode(c, 1.0, BEZ_MODE_LEGACY_FIXED, &f, &r) != BEZ_OK)
        return -1.0;
    /* c 是指针参数，必须先拷进真正的 [4] 数组再取地址。 */
    BPoint one[1][4];
    for (int i = 0; i < 4; ++i) one[0][i] = c[i];
    if (bezier_verify_path((const BPoint (*)[4]) one, 1, &f, 1e300, samples,
                           &r) != BEZ_OK)
        return -1.0;
    return r.max_err;
}

static int run_class(const Curve *q, double err_bound, int repeat) {
    BFlat f;
    BPathResult r;
    struct timespec t0;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = BEZ_E_ARG;
    for (int k = 0; k < repeat; ++k)
        rc = bezier_flatten_path((const BPoint (*)[4]) &q->c, 1, err_bound, &f,
                                 &r);
    double flat_total = ms_since(t0);
    if (rc != BEZ_OK) {
        printf("%-8s REJECTED rc=%d (%s)", q->name, rc, bezier_strerror(rc));
        if (r.bad_seg >= 0) printf(" bad_seg=%d", r.bad_seg);
        printf("\n");
        return 1;
    }
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = bezier_verify_path((const BPoint (*)[4]) &q->c, 1, &f, err_bound, 2048,
                            &r);
    double verify_ms = ms_since(t0);
    if (rc != BEZ_OK) {
        printf("%-8s VERIFY ERROR rc=%d\n", q->name, rc);
        return 1;
    }
    double legacy_err = legacy_fixed_error(q->c, 2048);
    printf("%-8s tol=%.6f  segs=%3d  max_err=%.9f  %s  flat_ms=%.6f  "
           "verify_ms=%.3f  (fixed8 err=%.6f)\n",
           q->name, r.tol, f.pieces, r.max_err, r.pass ? "PASS" : "FAIL",
           flat_total / repeat, verify_ms, legacy_err);
    if (!r.pass) {
        printf("         %d piece(s) exceed the bound E=%.6f; worst at piece %d "
               "(segment %d, piece-in-segment %d) t=[%.6f,%.6f] err=%.9f\n",
               r.violations, err_bound, r.worst_piece, r.worst_seg,
               r.worst_piece_in_seg, f.t0[r.worst_piece], f.t1[r.worst_piece],
               r.max_err);
        return 1;
    }
    return 0;
}

/* 反解容差的浮点性质：T = 4E/3 向下收敛后 (3/4)*T <= E 必须成立。 */
static int check_reverse_solve(void) {
    static const double es[] = {1e-9, 1e-6, 1e-3, 0.05, 0.1, 0.25, 1.0, 37.0,
                                1e3, 1e6};
    int bad = 0;
    printf("reverse-solve check: T=E/(3/4) rounded down, require (3/4)*T <= E\n");
    for (unsigned i = 0; i < sizeof(es) / sizeof(es[0]); ++i) {
        double T = bezier_tol_for_error(es[i]);
        double lhs = BEZ_CHORD_BOUND * T;
        int ok = (T > 0.0) && (lhs <= es[i]) && (es[i] - lhs) <= 2e-16 * es[i];
        printf("  E=%-10.4g T=%-22.17g (3/4)T=%-22.17g %s\n",
               es[i], T, lhs, ok ? "ok" : "BAD");
        if (!ok) ++bad;
    }
    double rejects[5] = {0.0, -1.0, -1e-9, NAN, INFINITY};
    for (unsigned i = 0; i < 5; ++i) {
        double T = bezier_tol_for_error(rejects[i]);
        int ok = (T < 0.0);
        printf("  E=%-10.4g rejected=%s\n", rejects[i], ok ? "yes" : "NO");
        if (!ok) ++bad;
    }
    return bad;
}

static const BPoint LEGACY_CURVE[4] = {{0,0},{20,40},{60,40},{80,0}};

/* 关掉新逻辑时的点序列导出，作为逐字节回归的输入。 */
static int dump_legacy(const char *dir) {
    BFlat f;
    BPathResult r;
    int rc = bezier_flatten_mode(LEGACY_CURVE, 0.5, BEZ_MODE_LEGACY_FIXED, &f,
                                 &r);
    if (rc != BEZ_OK) {
        fprintf(stderr, "legacy dump failed rc=%d\n", rc);
        return 1;
    }
    char path[1024];
    snprintf(path, sizeof path, "%s/fixed8.txt", dir);
    FILE *fp = fopen(path, "w");
    if (!fp) { perror(path); return 1; }
    for (int i = 0; i <= f.pieces; ++i)
        fprintf(fp, "%.17g %.17g\n", f.pts[i].x, f.pts[i].y);
    fclose(fp);
    printf("legacy (new logic off) points written to %s\n", path);
    return 0;
}

int main(int argc, char **argv) {
    const char *dir = "build";
    double err_bound = 0.25;
    if (argc > 1) {
        char *end = NULL;
        double v = strtod(argv[1], &end);
        if (end != argv[1] && v > 0.0) err_bound = v;
        else dir = argv[1];
    }
    if (argc > 2) dir = argv[2];

    Curve curves[4] = {
        {"gentle", {{0,0},{25,8},{75,8},{100,0}}},        /* 缓弯 */
        {"sharp",  {{0,0},{100,0},{100,100},{0,100}}},    /* 急弯 */
        {"S",      {{0,0},{100,50},{0,-50},{100,0}}},     /* S 形 */
        {"line",   {{0,0},{33,0.2},{66,-0.2},{100,0}}},   /* 近直线 */
    };

    int failures = 0;
    printf("adaptive flatten: shared chord-height bound E=%.6g, "
           "T = E/(3/4) = %.6f\n", err_bound, bezier_tol_for_error(err_bound));
    printf("REPEAT=%d for timing; fixed8 err = old fixed 8-segment error on the "
           "same curve\n\n", REPEAT);
    for (int i = 0; i < 4; ++i)
        failures += run_class(&curves[i], err_bound, REPEAT);

    printf("\n");
    failures += check_reverse_solve();

    printf("\n");
    failures += dump_legacy(dir);

    /* 负容差与退化控制点的拒绝，并指名段号。 */
    {
        BFlat f;
        BPathResult r;
        BPoint seg[3][4] = {
            {{0,0},{10,10},{20,-10},{30,0}},
            {{30,0},{40,10},{50,-10},{60,0}},
            {{60,0},{70,10},{NAN,0},{90,0}},       /* 段 2 控制点非有限 */
        };
        int rc = bezier_flatten_path((const BPoint (*)[4]) seg, 3, err_bound, &f,
                                     &r);
        printf("reject nan (segment 2): rc=%d bad_seg=%d %s\n", rc, r.bad_seg,
               rc == BEZ_E_DEGEN && r.bad_seg == 2 ? "ok" : "BAD");
        if (!(rc == BEZ_E_DEGEN && r.bad_seg == 2)) ++failures;

        BPoint one[1][4] = {{{0,0},{10,10},{20,-10},{30,0}}};
        rc = bezier_flatten_path((const BPoint (*)[4]) one, 1, -0.5, &f, &r);
        printf("reject negative tolerance: rc=%d bad_seg=%d "
               "(global parameter, not a segment) %s\n", rc, r.bad_seg,
               rc == BEZ_E_TOL && r.bad_seg == -1 ? "ok" : "BAD");
        if (!(rc == BEZ_E_TOL && r.bad_seg == -1)) ++failures;
    }

    /* 负对照：故意把整条急弯曲线当一条弦，验证器必须判超界。 */
    {
        BFlat f;
        BPathResult r;
        const BPoint *c = curves[1].c;
        f.pieces = 1;
        f.pts[0] = c[0];
        f.pts[1] = c[3];
        f.t0[0] = 0.0;
        f.t1[0] = 1.0;
        f.seg[0] = 0;
        int rc = bezier_verify_path((const BPoint (*)[4]) &curves[1].c, 1, &f,
                                    err_bound, 2048, &r);
        int ok = (rc == BEZ_OK) && (r.pass == 0) && (r.violations == 1);
        printf("negative control: 1-segment chord of 'sharp' -> %s "
               "(err=%.6f > E=%.6f, violations=%d)\n",
               ok ? "detected as FAIL (ok)" : "NOT detected (BAD)",
               r.max_err, err_bound, r.violations);
        if (!ok) ++failures;
    }

    printf("\n%s (%d failure(s))\n", failures ? "FAIL" : "OK", failures);
    return failures ? 1 : 0;
}
