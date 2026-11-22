#include "src/fit.h"
#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* 构造正 n 边形的便捷包装 */
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

typedef struct {
    const char *name;
    FPoly subj, target;
    double cx, cy, r;
} Group;

static void print_group(int id, const Group *g) {
    FitReport rep;
    int rc = fit_solve(&g->subj, &g->target, 96, 64, FIT_TOL, FIT_GAIN, &rep);
    if (rc) { printf("group %d solver error: %s\n", id, fit_strerror(rc)); return; }

    printf("=== group %d: %s ===\n", id, g->name);
    printf("target clip area = %.6f\n", rep.target_area);
    printf("%-4s %-12s %-10s %-9s %s\n", "n", "rel-err", "theta(deg)", "collinear", "note");
    for (int i = 0; i < rep.rows_n; ++i) {
        const FitRow *r = &rep.rows[i];
        const char *note = "";
        if (r->n == rep.chosen) note = "<= chosen";
        printf("%-4d %-12.3e %-10.3f %-9d %s\n", r->n, r->err,
               r->theta * 180.0 / M_PI, r->collinear, note);
    }
    printf("chosen n=%d  area-err=%.3e (limit %.0e: %s)  collinear verts=%d\n",
           rep.chosen, rep.chosen_err, rep.tol,
           rep.chosen_err <= rep.tol ? "PASS" : "FAIL", 0);
    printf("stop: code=%d  reason: %s\n", rep.stop_code, rep.stop_reason);
    printf("time %.3f ms\n\n", rep.ms);
}

int main(void) {
    /* K=256 正多边形作为参考圆窗口（独立高分辨率参照） */
    FPoly trisubj;
    trisubj.n = 3;
    trisubj.pts[0] = (FPoint){0, 0};
    trisubj.pts[1] = (FPoint){12, 0};
    trisubj.pts[2] = (FPoint){6, 10};
    Group groups[4] = {
        {"square subject, circle window",
         rect(-8, -8, 8, 8), ngon(256, 0, 0, 5, 0), 0, 0, 5},
        {"large subject, rotated-square window",
         rect(-20, -20, 20, 20), ngon(4, 1, -1, 6, 20), 1, -1, 6},
        {"triangle subject, circle window",
         trisubj, ngon(256, 5, 3, 4, 0), 5, 3, 4},
        {"large subject, pentagon window",
         rect(-30, -30, 30, 30), ngon(5, 0, 0, 7, 11), 0, 0, 7},
    };
    for (int i = 0; i < 4; ++i) print_group(i + 1, &groups[i]);

    /* 顶点不足三：拒绝并说明 */
    FPoly dup;
    dup.n = 2; dup.pts[0] = (FPoint){0, 0}; dup.pts[1] = (FPoint){1, 1};
    FPoly tri = ngon(3, 0, 0, 5, 0);
    FitReport rep;
    int rc = fit_solve(&dup, &tri, 16, 32, FIT_TOL, FIT_GAIN, &rep);
    printf("reject check: solve(2-vertex subject) -> rc=%d (%s)\n", rc,
           fit_strerror(rc));
    rc = fit_solve(&tri, &dup, 16, 32, FIT_TOL, FIT_GAIN, &rep);
    printf("reject check: solve(2-vertex target)  -> rc=%d (%s)\n", rc,
           fit_strerror(rc));

    /* 共线顶点单独标出：退化四边形含一个落在边上的顶点 */
    FPoly degen;
    degen.n = 4;
    degen.pts[0] = (FPoint){0, 0};
    degen.pts[1] = (FPoint){5, 0};
    degen.pts[2] = (FPoint){10, 0};
    degen.pts[3] = (FPoint){10, 10};
    int idx[FIT_MAX];
    int c = fit_collinear(&degen, idx, FIT_MAX);
    printf("collinear check: degenerate window has %d collinear vert(s):", c);
    for (int i = 0; i < c; ++i) printf(" v%d", idx[i]);
    printf("\n");
    int c2 = fit_collinear(&tri, idx, FIT_MAX);
    printf("collinear check: regular triangle has %d collinear vert(s)\n", c2);

    /* 关掉新逻辑：固定 4 顶点窗口路径，渲染产物与 render CLI 逐字节比对 */
    FPoly legacy_subj = rect(0, 0, 20, 12);
    FPoly legacy_hole;
    legacy_hole.n = 3;
    legacy_hole.pts[0] = (FPoint){6, 6};
    legacy_hole.pts[1] = (FPoint){16, 6};
    legacy_hole.pts[2] = (FPoint){11, 16};
    double box[4] = {0, 0, 20, 16};
    const FPoly *polys[2] = {&legacy_subj, &legacy_hole};
    rc = fit_write_pgm("build/legacy.pgm", polys, 2, box, 48, 32, 4);
    printf("legacy fixed-window render: fit_write_pgm -> rc=%d (compare with golden/)\n", rc);
    return 0;
}
