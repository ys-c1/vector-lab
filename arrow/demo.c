#include "scene.h"
#include "pipeline.h"
#include "arrow.h"
#include "export.h"
#include <math.h>
#include <stdio.h>
#include <time.h>

/* 独立切线参照：折线在弧长 target 处所在线段的方向，不调用 arrow.c */
static double ref_tangent(const RPoint *p, int n, double target) {
    double acc = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        double len = sqrt(dx * dx + dy * dy);
        if (acc + len >= target || i + 2 == n) return atan2(dy, dx);
        acc += len;
    }
    return 0.0;
}

static double ang_err(double a, double b) {
    double d = a - b;
    return fabs(atan2(sin(d), cos(d)));
}

/* 细四边形描边，让路径本身在图里可见 */
static int add_stroke(RScene *s, const RPoint *p, int n, double w) {
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        double len = sqrt(dx * dx + dy * dy);
        if (len < 1e-12) continue;
        double nx = -dy / len * w * 0.5, ny = dx / len * w * 0.5;
        RPoint q[4] = {
            {p[i].x + nx, p[i].y + ny}, {p[i+1].x + nx, p[i+1].y + ny},
            {p[i+1].x - nx, p[i+1].y - ny}, {p[i].x - nx, p[i].y - ny}
        };
        if (scene_add_poly(s, q, 4) != 0) return -1;
    }
    return 0;
}

#define W 96
#define H 72
#define ITERS 200

static int run(const char *name, const RPoint *p, int n, double density, const char *pgm) {
    RArrowSet set;
    int rc = arrow_layout(p, n, density, &set);
    if (rc != ARROW_OK) { printf("%s: layout rc=%d\n", name, rc); return 1; }
    double spacing = 1.0 / density, worst = 0.0;
    for (int k = 0; k < set.n; ++k) {
        double e = ang_err(set.a[k].angle, ref_tangent(p, n, (k + 0.5) * spacing));
        if (e > worst) worst = e;
    }
    RScene sc;
    scene_init(&sc);
    if (add_stroke(&sc, p, n, 1.0) != 0) return 1;
    RBox win;
    if (scene_bbox(&sc, &win) != 0) return 1;
    double pad = 4.0;
    win.minx -= pad; win.miny -= pad; win.maxx += pad; win.maxy += pad;
    unsigned char buf[W * H];
    RPlace place = {0.0, 0.0, 0.0, 1.0};
    clock_t t0 = clock();
    for (int it = 0; it < ITERS; ++it)
        pipeline_render_arrows(&sc, place, win, W, H, p, n, density, 4.0, buf);
    double ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC / ITERS;
    if (export_pgm(pgm, buf, W, H) != 0) return 1;
    printf("%s: arrows=%d err=%.4f rad ms=%.3f\n", name, set.n, worst, ms);
    return worst > 0.5 ? 1 : 0;
}

int main(void) {
    /* 缓弯：浅正弦折线 */
    RPoint gentle[11];
    for (int i = 0; i < 11; ++i) {
        gentle[i].x = i * 6.0;
        gentle[i].y = 6.0 * sin(i * 6.0 * 0.15);
    }
    /* 急弯：90 度折角 */
    RPoint sharp[6] = {{0,0},{20,0},{20,20},{40,20},{40,40},{60,40}};
    /* 回折：去程后 180 度折返 */
    RPoint hairpin[4] = {{0,10},{60,10},{60,12},{0,12}};
    int bad = 0;
    bad |= run("gentle ", gentle, 11, 0.08, "demo_gentle.pgm");
    bad |= run("sharp  ", sharp, 6, 0.08, "demo_sharp.pgm");
    bad |= run("hairpin", hairpin, 4, 0.08, "demo_hairpin.pgm");
    return bad;
}
