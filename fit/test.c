#include "src/fit.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static FPoly ngon(int n, double cx, double cy, double r, double deg) {
    FPoly p;
    fit_regular(n, cx, cy, r, deg * M_PI / 180.0, &p);
    return p;
}

static FPoly rect(double x0, double y0, double x1, double y1) {
    FPoly p;
    p.n = 4;
    p.pts[0] = (FPoint){x0, y0}; p.pts[1] = (FPoint){x1, y0};
    p.pts[2] = (FPoint){x1, y1}; p.pts[3] = (FPoint){x0, y1};
    return p;
}

int main(void) {
    /* 基础图元 */
    FPoly sq = rect(0, 0, 10, 10);
    CK(fabs(fit_area(&sq) - 100.0) < 1e-9);
    FPoly win4 = ngon(4, 2, 2, 100, 0); /* 大窗口，整个方块在内 */
    FPoly out;
    CK(fit_clip(&sq, &win4, &out) == 0);
    CK(fabs(fit_area(&out) - 100.0) < 1e-6);
    CK(fit_clip(&sq, &sq, &out) == 0 && fabs(fit_area(&out) - 100.0) < 1e-6);

    /* 顶点不足三：拒绝 */
    FPoly dup;
    dup.n = 2; dup.pts[0] = (FPoint){0, 0}; dup.pts[1] = (FPoint){1, 1};
    FitReport rep;
    CK(fit_solve(&dup, &sq, 16, 32, FIT_TOL, FIT_GAIN, &rep) == -2);
    CK(fit_solve(&sq, &dup, 16, 32, FIT_TOL, FIT_GAIN, &rep) == -3);
    FPoly bad;
    bad.n = 2; bad.pts[0] = (FPoint){0, 0}; bad.pts[1] = (FPoint){1, 1};
    CK(fit_regular(2, 0, 0, 1, 0, &bad) == -2);

    /* 共线顶点检测 */
    FPoly degen;
    degen.n = 4;
    degen.pts[0] = (FPoint){0, 0}; degen.pts[1] = (FPoint){5, 0};
    degen.pts[2] = (FPoint){10, 0}; degen.pts[3] = (FPoint){10, 10};
    int idx[FIT_MAX];
    CK(fit_collinear(&degen, idx, FIT_MAX) == 1 && idx[0] == 1);
    CK(fit_collinear(&sq, idx, FIT_MAX) == 0);

    /* 四组反解：容差、停止码、单调性 */
    FPoly trisubj;
    trisubj.n = 3;
    trisubj.pts[0] = (FPoint){0, 0}; trisubj.pts[1] = (FPoint){12, 0};
    trisubj.pts[2] = (FPoint){6, 10};
    struct { const char *name; FPoly subj, target; int expect_stop; } G[4] = {
        {"g1", rect(-8, -8, 8, 8), ngon(256, 0, 0, 5, 0), FIT_STOP_TOL},
        {"g2", rect(-20, -20, 20, 20), ngon(4, 1, -1, 6, 20), FIT_STOP_EXACT},
        {"g3", trisubj, ngon(256, 5, 3, 4, 0), FIT_STOP_TOL},
        {"g4", rect(-30, -30, 30, 30), ngon(5, 0, 0, 7, 11), FIT_STOP_EXACT},
    };
    for (int g = 0; g < 4; ++g) {
        int rc = fit_solve(&G[g].subj, &G[g].target, 96, 64, FIT_TOL, FIT_GAIN, &rep);
        CK(rc == 0);
        CK(rep.chosen >= 3);
        CK(rep.chosen_err <= FIT_TOL);
        CK(rep.stop_code == G[g].expect_stop);
        CK(rep.rows_n >= 3);
        for (int i = 1; i < rep.rows_n; ++i)
            CK(rep.rows[i].n == rep.rows[i-1].n + 1);
        for (int i = 0; i < rep.rows_n; ++i)
            CK(rep.rows[i].collinear == 0);
        printf("%s: chosen n=%d err=%.3e stop=%d time=%.3f ms\n",
               G[g].name, rep.chosen, rep.chosen_err, rep.stop_code, rep.ms);
    }

    /* 精确地板：g2 在 n=4、g4 在 n=5 处误差为零，之后回升 */
    fit_solve(&G[1].subj, &G[1].target, 12, 64, FIT_TOL, FIT_GAIN, &rep);
    CK(fabs(rep.rows[4-3].err) < FIT_EPS);
    CK(rep.rows[5-3].err > rep.rows[4-3].err);
    fit_solve(&G[3].subj, &G[3].target, 12, 64, FIT_TOL, FIT_GAIN, &rep);
    CK(fabs(rep.rows[5-3].err) < FIT_EPS);

    /* 容差停止锁定的是“首个过线 n”，不是 lookahead 里的更优 n */
    fit_solve(&G[0].subj, &G[0].target, 96, 64, FIT_TOL, FIT_GAIN, &rep);
    int first_pass = -1;
    for (int i = 0; i < rep.rows_n; ++i)
        if (rep.rows[i].err <= FIT_TOL) { first_pass = rep.rows[i].n; break; }
    CK(first_pass > 0 && rep.chosen == first_pass);

    /* 增益地板：圆目标误差按 ~1/n^2 收敛，相对增益约 2/n。
       把增益阈值放宽到 5%，n≈40 起连续 3 步增益低于阈值即判枯竭，
       此时还远未触及 1e-3 容差，验证 GAIN 停止路径真实可达 */
    {
        FPoly subj = rect(-30, -30, 30, 30);
        FPoly circ = ngon(256, 0, 0, 5, 0);
        FitReport r2;
        int rc = fit_solve(&subj, &circ, 200, 32, FIT_TOL, 0.05, &r2);
        CK(rc == 0);
        CK(r2.stop_code == FIT_STOP_GAIN);
        CK(r2.chosen >= 3 && r2.chosen_err > FIT_TOL);
    }

    /* 顶点上限：极小 nmax 下既进不了容差也攒不够停滞步数 -> CAP */
    {
        FPoly subj = rect(-30, -30, 30, 30);
        FPoly circ = ngon(256, 0, 0, 5, 0);
        FitReport r3;
        int rc = fit_solve(&subj, &circ, 6, 32, FIT_TOL, FIT_GAIN, &r3);
        CK(rc == 0);
        CK(r3.stop_code == FIT_STOP_CAP);
        CK(r3.rows_n == 4);              /* n = 3..6 */
        CK(r3.chosen >= 3 && r3.chosen <= 6);
        CK(r3.chosen_err > FIT_TOL);
    }

    /* 关掉新逻辑：固定窗口路径渲染，字节由 Makefile 与 render/golden 比对 */
    FPoly legacy_subj = rect(0, 0, 20, 12);
    FPoly legacy_hole;
    legacy_hole.n = 3;
    legacy_hole.pts[0] = (FPoint){6, 6}; legacy_hole.pts[1] = (FPoint){16, 6};
    legacy_hole.pts[2] = (FPoint){11, 16};
    double box[4] = {0, 0, 20, 16};
    const FPoly *polys[2] = {&legacy_subj, &legacy_hole};
    CK(fit_write_pgm("build/legacy.pgm", polys, 2, box, 48, 32, 4) == 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
