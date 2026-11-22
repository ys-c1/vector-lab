#define _POSIX_C_SOURCE 199309L
/*
 * 四组角点对应关系的拟合演示。
 *
 *   make -C fit demo            # 新逻辑（线性方程组）
 *   make -C fit demo-trial      # 旧逻辑（逐个试，新逻辑关闭）
 *   make -C fit bytecheck       # 两种模式确定性产物逐字节比对
 *
 * 每组打印：解出的三个未知量 (rot, tx, ty)、逐角点残差、最大/RMS 残差、
 * 单次求解耗时与万次平均耗时。耗时只打到屏幕，不进确定性产物。
 */
#include "src/fit.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define NGROUP 6
#define NREP   10000

static const FBounds bounds = { 0.9, 1.1, 0.6, 5000.0, 0.5 };

typedef struct {
    const char *name;
    FPoint src[4], dst[4];
    int n;
} Group;

static FTransform tr_gt(double r, double tx, double ty) {
    FTransform t = {r, tx, ty, 1.0};
    return t;
}

static void map4(FPoint *dst, const FPoint *src, FTransform gt,
                 const FPoint *noise) {
    for (int i = 0; i < 4; ++i) {
        FPoint q = fit_apply(gt, src[i]);
        dst[i].x = q.x + (noise ? noise[i].x : 0.0);
        dst[i].y = q.y + (noise ? noise[i].y : 0.0);
    }
}

static double bench_avg(const FPoint *s, const FPoint *d, int n, int trial,
                        double *single_ns) {
    double tmin = 1e300;
    FFitResult last;
    for (int rep = 0; rep < 7; ++rep) {
        double t0;
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        for (int k = 0; k < NREP; ++k)
            last = trial ? fit_solve_trial(s, d, n, &bounds)
                         : fit_solve_linear(s, d, n, &bounds);
        clock_gettime(CLOCK_MONOTONIC, &b);
        t0 = ((double)b.tv_sec - a.tv_sec) * 1e9 +
             ((double)b.tv_nsec - a.tv_nsec);
        if (t0 < tmin) tmin = t0;
    }
    *single_ns = last.solve_ns;
    return tmin / NREP;
}

/* 确定性产物：不含任何时间量，两种编译模式下良态组必须逐字节一致 */
static void dump_record(FILE *f, const char *name, const FFitResult *r) {
    fprintf(f, "group=%s status=%s rot=%.9f tx=%.9f ty=%.9f scale=%.9f "
            "maxres=%.9f rms=%.9f",
            name, fit_status_name(r->status), r->tr.rot, r->tr.tx, r->tr.ty,
            r->tr.scale_unused, r->max_residual, r->rms_residual);
    for (int i = 0; i < 4; ++i) fprintf(f, " r%d=%.9f", i, r->per_corner[i]);
    if (r->status == FIT_E_DEGENERATE)
        fprintf(f, " conflict=#%d(%s)/#%d(%s)",
                r->conflict_a, r->conflict_axis[0],
                r->conflict_b, r->conflict_axis[1]);
    fputc('\n', f);
}

int main(int argc, char **argv) {
    const char *outpath = (argc >= 2) ? argv[1] : NULL;
    int trial_mode = 0;
#ifdef FIT_NO_LINEAR
    trial_mode = 1;
#endif

    FPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    Group g[NGROUP];
    memset(g, 0, sizeof g);

    /* 组1：精确对应，真值 rot=0.35, tx=120, ty=-60 */
    g[0].name = "exact"; g[0].n = 4;
    memcpy(g[0].src, sq, sizeof sq);
    map4(g[0].dst, sq, tr_gt(0.35, 120.0, -60.0),
         (FPoint[4]){{0,0},{0,0},{0,0},{0,0}});

    /* 组2：亚像素扰动，最大扰动 0.32px，应在半像素容差内通过 */
    g[1].name = "subpixel"; g[1].n = 4;
    memcpy(g[1].src, sq, sizeof sq);
    map4(g[1].dst, sq, tr_gt(0.35, 120.0, -60.0),
         (FPoint[4]){{0.20,-0.15},{-0.10,0.25},{0.15,0.10},{-0.20,-0.10}});

    /* 组3：单点偏 1.5px，残差必然超半像素，应拒绝并指出超差角点 */
    g[2].name = "over-tolerance"; g[2].n = 4;
    memcpy(g[2].src, sq, sizeof sq);
    map4(g[2].dst, sq, tr_gt(0.35, 120.0, -60.0),
         (FPoint[4]){{1.5,0.0},{0,0},{0,0},{0,0}});

    /* 组4：源角点全部重合，3 未知量系统秩亏（转角无约束），需指出冲突的两条 */
    g[3].name = "degenerate-coincident"; g[3].n = 4;
    for (int i = 0; i < 4; ++i) { g[3].src[i].x = 5.0; g[3].src[i].y = 5.0; }
    g[3].dst[0] = (FPoint){0,0}; g[3].dst[1] = (FPoint){1,0};
    g[3].dst[2] = (FPoint){1,1}; g[3].dst[3] = (FPoint){0,1};

    /* 组5：共线但数据不相容（隐含缩放≈1 不越界），刚性模型不秩亏，残差超半像素 */
    g[4].name = "collinear-inconsistent"; g[4].n = 4;
    for (int i = 0; i < 4; ++i) { g[4].src[i].x = 4.0 * i; g[4].src[i].y = 0.0; }
    g[4].dst[0] = (FPoint){0,0}; g[4].dst[1] = (FPoint){4,0};
    g[4].dst[2] = (FPoint){8,1.2}; g[4].dst[3] = (FPoint){12,0};

    /* 组6：真实缩放 3.0 越界 [0.9,1.1]，应越界拒绝 */
    g[5].name = "out-of-bounds-scale"; g[5].n = 4;
    memcpy(g[5].src, sq, sizeof sq);
    for (int i = 0; i < 4; ++i) {
        g[5].dst[i].x = sq[i].x * 3.0;
        g[5].dst[i].y = sq[i].y * 3.0;
    }

    FILE *rf = outpath ? fopen(outpath, "w") : NULL;
    if (outpath && !rf) { fprintf(stderr, "cannot write %s\n", outpath); return 2; }

    printf("solver: %s\n", trial_mode ? "trial (new logic OFF)" : "linear equations (new logic ON)");
    printf("bounds: scale [%.2f,%.2f]  |rot|<=%.2f rad  |t|<=%.0f px  tol=%.2f px\n\n",
           bounds.min_scale, bounds.max_scale, bounds.max_rot_abs,
           bounds.max_t_abs, bounds.pixel_tol);

    int allok = 1;
    for (int gi = 0; gi < NGROUP; ++gi) {
        FFitResult r = trial_mode
            ? fit_solve_trial(g[gi].src, g[gi].dst, g[gi].n, &bounds)
            : fit_solve_linear(g[gi].src, g[gi].dst, g[gi].n, &bounds);
        double single, avg = bench_avg(g[gi].src, g[gi].dst, g[gi].n,
                                       trial_mode, &single);

        printf("=== group %d: %s ===\n", gi + 1, g[gi].name);
        printf("  status   : %s\n", fit_status_name(r.status));
        printf("  unknowns : rot=%.6f rad (%.4f deg)  tx=%.6f px  ty=%.6f px\n",
               r.tr.rot, r.tr.rot * 180.0 / M_PI, r.tr.tx, r.tr.ty);
        printf("  residual : max=%.4f px  rms=%.4f px  per-corner:",
               r.max_residual, r.rms_residual);
        for (int i = 0; i < g[gi].n; ++i) printf(" %.4f", r.per_corner[i]);
        putchar('\n');
        printf("  time     : single %.0f ns, avg over %d solves %.1f ns\n",
               single, NREP, avg);
        if (r.status == FIT_E_DEGENERATE)
            printf("  conflict : equation of corner #%d (%s row) vs corner #%d (%s row)\n",
                   r.conflict_a, r.conflict_axis[0],
                   r.conflict_b, r.conflict_axis[1]);
        printf("  detail   : %s\n\n", r.detail);

        if (rf) dump_record(rf, g[gi].name, &r);
        int expect_ok = (gi == 0 || gi == 1);
        int expect_residual = (gi == 2 || gi == 4);
        int expect_degenerate = (gi == 3);
        int expect_bounds = (gi == 5);
        if (expect_ok && r.status != FIT_OK) allok = 0;
        if (expect_residual && r.status != FIT_E_RESIDUAL) allok = 0;
        if (expect_degenerate && r.status != FIT_E_DEGENERATE) allok = 0;
        if (expect_bounds && r.status != FIT_E_BOUNDS) allok = 0;
    }

    if (rf) fclose(rf);
    printf("expectation check: %s\n", allok ? "all groups matched expected verdict" : "MISMATCH");
    return allok ? 0 : 1;
}
