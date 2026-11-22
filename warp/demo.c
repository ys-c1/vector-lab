#define _POSIX_C_SOURCE 199309L
#include "warp.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

/* 强变形基准曲线：(0,0),(0,14),(10,-10),(10,6)。
 * 非对称，等参数 t 采样在弧长上明显不均：t=0.5 的累计弧长与总长一半差约 4%，
 * 半弧长对应的参数 t*≈0.5204 而不是 0.5。 */
static WPoint kCurve[4] = {{0,0},{0,14},{10,-10},{10,6}};

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* 独立参照：定步长梯形积分（与库内自适应 Simpson 是两条计算路径），
 * 用来独立核对 S(t)、总长与解点残差。 */
static double ref_arclen(const WPoint c[4], double t, int steps) {
    double acc = 0.0, prev = 0.0;
    for (int i = 0; i <= steps; ++i) {
        double u = t * (double)i / (double)steps;
        double vx = 3*(1-u)*(1-u)*(c[1].x-c[0].x)
                  + 6*(1-u)*u*(c[2].x-c[1].x)
                  + 3*u*u*(c[3].x-c[2].x);
        double vy = 3*(1-u)*(1-u)*(c[1].y-c[0].y)
                  + 6*(1-u)*u*(c[2].y-c[1].y)
                  + 3*u*u*(c[3].y-c[2].y);
        double v = sqrt(vx*vx + vy*vy);
        if (i == 0 || i == steps) acc += 0.5 * v;
        else acc += v;
        if (i == steps) prev = v;
    }
    (void)prev;
    return acc * (t / (double)steps);
}

static const char *status_name(int s) {
    switch (s) {
        case WARP_OK: return "OK";
        case WARP_NO_CONVERGE: return "NO_CONVERGE";
        case WARP_OSCILLATING: return "OSCILLATING";
        default: return "ERROR";
    }
}

int main(void) {
    double total = warp_arclen(kCurve, 1.0);
    double ref_total = ref_arclen(kCurve, 1.0, 200000);
    double target = 0.5 * total;
    printf("基准曲线控制点 (0,0) (0,14) (10,-10) (10,6)\n");
    printf("总长 L=%.12f，独立梯形参照 L_ref=%.12f，差 %.3e\n",
           total, ref_total, fabs(total - ref_total));
    printf("目标弧长 s=%.12f（总长的一半）；不动点 t_{k+1}=clamp(t_k+w(t_k)*(s-S(t_k))/L)\n",
           target);
    printf("自适应 w(t)=min(cap,0.9L/v(t))，cap=min(0.9,1.8L/vmax)，保证全局 q<1\n\n");

    /* ---- 四组初值：迭代次数 / 残差 / 耗时 ---- */
    double t0s[4] = {0.0, 0.25, 0.5, 1.0};
    WarpCfg cfg = {1e-10, 1e-12, 64, 0.0, 0.8};
    printf("%-6s %-13s %6s %14s %12s %10s %12s %10s\n",
           "初值t0", "状态", "迭代数", "末轮t", "弧长残差", "步长", "收敛因子q", "耗时(us)");
    double t_solved = 0.0;
    for (int i = 0; i < 4; ++i) {
        WPoint p; WarpReport r;
        double t1 = now_ms();
        int rc = warp_correct(kCurve, target, t0s[i], WARP_F_FIXPOINT, &cfg, &p, &r);
        double t2 = now_ms();
        printf("%-6.2f %-13s %6d %14.10f %14.3e %10.2e %12.4f %10.3f\n",
               t0s[i], status_name(rc), r.iters, r.t, r.residual, r.step,
               r.iters >= 2 ? r.rate : 0.0, (t2 - t1) * 1e3);
        if (rc != WARP_OK || r.residual > 1e-10 || r.iters > 64) {
            printf("  !! 初值 %.2f 未达标：%s\n", t0s[i], status_name(rc));
            return 1;
        }
        if (i == 0) t_solved = r.t;
        else if (fabs(r.t - t_solved) > 1e-10) {
            printf("  !! 解随初值漂移，不满足不动点唯一性\n");
            return 1;
        }
    }
    /* 独立参照核对解点残差 */
    double ref_resid = fabs(target - ref_arclen(kCurve, t_solved, 200000));
    printf("\n解 t*=%.12f；独立梯形积分复核残差 %.3e（要求 <=1e-9）\n",
           t_solved, ref_resid);
    if (ref_resid > 1e-9) { printf("  !! 独立复核失败\n"); return 1; }

    /* ---- 不收敛拒绝：压上限，必须拒并说明初值 ---- */
    printf("\n[拒绝案例]\n");
    {
        WarpCfg tight = {1e-10, 1e-12, 2, 0.0, 0.8};
        double rejects[2] = {0.0, 1.0};
        for (int i = 0; i < 2; ++i) {
            WPoint p; WarpReport r;
            int rc = warp_correct(kCurve, target, rejects[i], WARP_F_FIXPOINT, &tight, &p, &r);
            printf("  初值 t0=%.1f, max_iter=2 -> %s iters=%d 残差=%.3e（%s）\n",
                   rejects[i], status_name(rc), r.iters, r.residual,
                   rc == WARP_NO_CONVERGE ? "已拒绝，不出点" : "异常");
            if (rc != WARP_NO_CONVERGE) return 1;
        }
    }

    /* ---- 振荡专项：omega 逼近/越过压缩边界，2-周期来回 ---- */
    printf("\n[振荡案例]\n");
    {
        struct { double omega, eta, sfrac, t0; int maxit; } cases[3] = {
            {1.99, 0.99, 0.999, 0.0, 64},
            {1.90, 0.50, 0.970, 0.02, 40},
            {0.50, 0.80, 0.500, 0.0, 64},   /* 对照：正常收敛，不得误报振荡 */
        };
        for (int i = 0; i < 3; ++i) {
            WarpCfg c2 = {1e-13, 1e-15, cases[i].maxit, cases[i].omega, cases[i].eta};
            WPoint p; WarpReport r;
            int rc = warp_correct(kCurve, cases[i].sfrac * total, cases[i].t0,
                                  WARP_F_FIXPOINT, &c2, &p, &r);
            printf("  omega=%.2f eta=%.2f s=%.3fL t0=%.2f -> %-13s 周期=%d 触发步=%d iters=%d 残差=%.3e\n",
                   cases[i].omega, cases[i].eta, cases[i].sfrac, cases[i].t0,
                   status_name(rc), r.osc_period, r.osc_step, r.iters, r.residual);
            if (i < 2 && rc != WARP_OSCILLATING) {
                printf("  !! 预期振荡未检出\n"); return 1;
            }
            if (i == 2 && rc == WARP_OSCILLATING) {
                printf("  !! 正常收敛被误判振荡\n"); return 1;
            }
        }
    }

    /* ---- 关闭新逻辑：legacy 固定 8 次，产 PGM 供逐字节比对 ---- */
    printf("\n[兼容路径]\n");
    enum { N = 17 };
    WPoint legacy[N], fixed[N];
    WarpReport reps[N];
    if (warp_equally_spaced(kCurve, N, 0, 0, legacy, 0) != WARP_OK) return 1;
    if (warp_equally_spaced(kCurve, N, WARP_F_FIXPOINT, &cfg, fixed, reps) != WARP_OK) return 1;
    double minx = -1, miny = -11, maxx = 11, maxy = 15;
    if (warp_save_pgm("build/legacy.pgm", legacy, N, minx, miny, maxx, maxy, 120, 120) != WARP_OK)
        return 1;
    /* 用 legacy 同样的点序列再写一份，证明导出路径确定性；compat 目标做真比对 */
    if (warp_save_pgm("build/legacy_copy.pgm", legacy, N, minx, miny, maxx, maxy, 120, 120) != WARP_OK)
        return 1;
    if (warp_save_pgm("build/fixpoint.pgm", fixed, N, minx, miny, maxx, maxy, 120, 120) != WARP_OK)
        return 1;
    if (warp_save_report_csv("build/fixpoint_reports.csv", reps, N) != WARP_OK) return 1;
    printf("  写出 build/legacy.pgm build/legacy_copy.pgm build/fixpoint.pgm\n");
    printf("  写出 build/fixpoint_reports.csv（逐点状态/迭代/残差）\n");
    printf("  compat: make -C warp compat 比对 legacy 与固定 8 次路径逐字节一致\n");

    /* 耗时基准：千次求解 */
    double t1 = now_ms();
    volatile double sink = 0.0;
    for (int k = 0; k < 1000; ++k) {
        WarpReport r;
        warp_correct(kCurve, target, 0.5, WARP_F_FIXPOINT, &cfg, 0, &r);
        sink += r.t;
    }
    double t2 = now_ms();
    printf("\n千次不动点求解耗时 %.3f ms（sink=%.6f）\n", t2 - t1, (double)sink);
    return 0;
}
