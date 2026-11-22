/* 定点化验证报告：
 *   1. 四组场景：按目标精度反解 q，定点渲染与浮点参照图逐像素比对，报位数/差异像素/耗时；
 *   2. 精度不足演示：故意给过粗的 eps，差异非零时拒绝并补位到零，报告差几位；
 *   3. 溢出场景：坐标量级 * 2^q 超出安全范围，单独报告最劣量级与允许的最大 q。
 * 浮点路径（pipeline_render）为参照，不参与新逻辑。
 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/fixed.h"
#include "src/export.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static int diffpix(const unsigned char *a, const unsigned char *b, size_t n) {
    int d = 0;
    for (size_t i = 0; i < n; ++i) if (a[i] != b[i]) ++d;
    return d;
}

typedef struct { const char *name; RScene s; RPlace pl; int w, h; } Scen;

/* 渲染一组场景：浮点参照 + q 位定点，返回差异像素数；*qneed 输出归零所需的 q */
static int run_one(const Scen *sc, double eps, int *outq, int *outdiff,
                   double *tf, double *tx, int writepgm) {
    RBox win;
    if (scene_bbox(&sc->s, &win) != 0) return -1;
    int q = fixed_bits_for_eps(eps);
    size_t n = (size_t)sc->w * sc->h;
    unsigned char *bf = malloc(n), *bx = malloc(n);
    if (!bf || !bx) { free(bf); free(bx); return -1; }
    double t0 = now_ms();
    if (pipeline_render(&sc->s, sc->pl, win, sc->w, sc->h, bf) != 0) { free(bf); free(bx); return -1; }
    double t1 = now_ms();
    if (pipeline_render_fixed(&sc->s, sc->pl, win, sc->w, sc->h, q, bx) != 0) { free(bf); free(bx); return -1; }
    double t2 = now_ms();
    int d = diffpix(bf, bx, n);
    int qneed = q;
    if (d) { /* 位数不足：逐位补到差异归零 */
        for (int qq = q + 1; qq <= 40; ++qq) {
            if (pipeline_render_fixed(&sc->s, sc->pl, win, sc->w, sc->h, qq, bx) != 0) break;
            if (diffpix(bf, bx, n) == 0) { qneed = qq; break; }
        }
    }
    if (writepgm) {
        char path[256];
        mkdir("build", 0755);
        mkdir("build/fx", 0755);
        snprintf(path, sizeof path, "build/fx/%s_float.pgm", sc->name);
        export_pgm(path, bf, sc->w, sc->h);
        snprintf(path, sizeof path, "build/fx/%s_fixed.pgm", sc->name);
        export_pgm(path, bx, sc->w, sc->h);
    }
    *outq = q; *outdiff = d; *tf = t1 - t0; *tx = t2 - t1;
    free(bf); free(bx);
    return qneed;
}

static void star(RPoint *p, int n, double cx, double cy, double ro, double ri) {
    for (int i = 0; i < n; ++i) {
        double a = 2.0 * M_PI * i / n - M_PI / 2;
        double r = (i % 2) ? ri : ro;
        p[i].x = cx + r * cos(a);
        p[i].y = cy + r * sin(a);
    }
}

int main(void) {
    const double EPS = 1e-9; /* 目标精度：9 位十进制小数 */
    int fails = 0;

    Scen sc[4];
    /* 1 轴对齐方块 */
    RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    sc[0].name = "square-64"; sc[0].pl = (RPlace){0, 0, 0, 1.0}; sc[0].w = 64; sc[0].h = 64;
    scene_init(&sc[0].s); scene_add_poly(&sc[0].s, sq, 4);
    /* 2 旋转矩形（变换走定点） */
    RPoint rc[4] = {{-8,-3},{8,-3},{8,3},{-8,3}};
    sc[1].name = "rotrect-96"; sc[1].pl = (RPlace){1, 2, 0.3, 1.2}; sc[1].w = 96; sc[1].h = 96;
    scene_init(&sc[1].s); scene_add_poly(&sc[1].s, rc, 4);
    /* 3 凹多边形五角星 */
    RPoint st[10];
    star(st, 10, 0, 0, 10, 4.2);
    sc[2].name = "star-128"; sc[2].pl = (RPlace){0.5, -0.5, 0.15, 1.0}; sc[2].w = 128; sc[2].h = 128;
    scene_init(&sc[2].s); scene_add_poly(&sc[2].s, st, 10);
    /* 4 多多边形 + 缩放旋转 */
    RPoint tri[3] = {{-9,-9},{-2,-8},{-6,-1}};
    RPoint dia[4] = {{3,-6},{9,-3},{3,0},{-3,-3}};
    RPoint hex[6];
    star(hex, 6, 5, 5, 4, 4);
    sc[3].name = "multi-128"; sc[3].pl = (RPlace){0, 0, 0.08, 1.5}; sc[3].w = 128; sc[3].h = 128;
    scene_init(&sc[3].s);
    scene_add_poly(&sc[3].s, tri, 3);
    scene_add_poly(&sc[3].s, dia, 4);
    scene_add_poly(&sc[3].s, hex, 6);

    printf("== 四组场景（目标精度 eps=%.0e，%d 位十进制小数）==\n",
           EPS, (int)ceil(-log10(EPS)));
    printf("%-12s %6s %6s %8s %10s %10s %s\n",
           "场景", "十进制", "二进制q", "差异像素", "浮点ms", "定点ms", "结论");
    for (int i = 0; i < 4; ++i) {
        int q, d; double tf, tx;
        int qneed = run_one(&sc[i], EPS, &q, &d, &tf, &tx, 1);
        if (qneed < 0) { printf("%-12s 渲染失败\n", sc[i].name); ++fails; continue; }
        const char *verdict = d == 0 ? "通过" : "拒绝";
        printf("%-12s %6d %6d %8d %10.3f %10.3f %s\n",
               sc[i].name, (int)ceil(-log10(EPS)), q, d, tf, tx, verdict);
        if (d != 0) {
            printf("  拒绝：q=%d 差 %d 像素，补到 q=%d 才归零，差 %d 位\n",
                   q, d, qneed, qneed - q);
            ++fails;
        }
    }

    printf("\n== 精度不足拒绝演示（场景 rotrect-96，故意放粗 eps）==\n");
    {
        double bad = 1e-2;
        int q, d; double tf, tx;
        int qneed = run_one(&sc[1], bad, &q, &d, &tf, &tx, 0);
        while (d == 0 && q > 1) { /* 若 q=7 仍巧合归零，继续放粗直到露出偏差 */
            bad *= 4.0;
            qneed = run_one(&sc[1], bad, &q, &d, &tf, &tx, 0);
        }
        if (d == 0) printf("未能构造出偏差（场景对该精度不敏感）\n");
        else printf("eps=%.2e -> q=%d：差异 %d 像素，拒绝；需 q=%d 才归零，差 %d 位\n",
                    bad, q, d, qneed, qneed - q);
    }

    printf("\n== 溢出场景（单独报告，不参与渲染）==\n");
    {
        struct { const char *name; double mag; } ov[2] = {
            {"huge-1e13", 1e13}, {"huge-1e15", 1e15}
        };
        for (int i = 0; i < 2; ++i) {
            RPoint p[4] = {{-ov[i].mag,-ov[i].mag},{ov[i].mag,-ov[i].mag},
                           {ov[i].mag,ov[i].mag},{-ov[i].mag,ov[i].mag}};
            RScene s; scene_init(&s); scene_add_poly(&s, p, 4);
            RBox win; scene_bbox(&s, &win);
            int q = fixed_bits_for_eps(EPS);
            double worst; int qmax;
            int r = fixed_check_overflow(&s, &win, q, &worst, &qmax);
            if (r == 1) {
                double best = ldexp(1.0, -qmax);
                printf("%s：溢出。坐标量级 %.0e，q=%d 需要约 %d 位（上限 %d 位）；"
                       "该量级最多 q=%d，可达精度 %.3e，达不到目标 %.0e\n",
                       ov[i].name, worst, q,
                       (int)ceil(log2(worst)) + q, FX_INT_LIM_BIT,
                       qmax, best, EPS);
            } else {
                printf("%s：未溢出（worst=%.0e qmax=%d）\n", ov[i].name, worst, qmax);
            }
        }
    }

    printf("\n%s\n", fails ? "RESULT: FAIL" : "RESULT: OK");
    return fails ? 1 : 0;
}
