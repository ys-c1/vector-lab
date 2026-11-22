#ifndef ZGROUPS_H
#define ZGROUPS_H
/* 四组反解场景（两层/三层/交错/同框）+ 零面积接触组 + 环依赖组，
 * 以及参照渲染与报告行，供 demo.c / test.c 共用。 */
#include "src/zorder.h"
#include <stdio.h>
#include <time.h>

typedef struct {
    const char *name;
    int n;
    ZPoly polys[ZO_MAX];
    unsigned char colors[ZO_MAX];
    int prio[ZO_MAX]; /* 真值层序：值大者盖人；环依赖组不用 */
} ZGroup;

static int zg_inside(const ZPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        ZPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static ZPoly zg_rect(double x0, double y0, double x1, double y1) {
    ZPoly p;
    p.n = 4;
    p.pts[0] = (ZPoint){x0, y0};
    p.pts[1] = (ZPoint){x1, y0};
    p.pts[2] = (ZPoint){x1, y1};
    p.pts[3] = (ZPoint){x0, y1};
    return p;
}

static ZBox zg_win(const ZGroup *g) {
    ZBox u, b;
    zorder_poly_bbox(&g->polys[0], &u);
    for (int k = 1; k < g->n; ++k) {
        zorder_poly_bbox(&g->polys[k], &b);
        if (b.minx < u.minx) u.minx = b.minx;
        if (b.miny < u.miny) u.miny = b.miny;
        if (b.maxx > u.maxx) u.maxx = b.maxx;
        if (b.maxy > u.maxy) u.maxy = b.maxy;
    }
    return u;
}

/* 参照渲染：逐子采样取 prio 最大者颜色，与 zorder_compose 同一平均规则。
 * 输出即"期望的遮挡结果"，作为反解的输入目标图。 */
static void zg_ref_render(const ZGroup *g, ZBox win, int w, int h, int ss,
                          unsigned char *out) {
    double dx = (win.maxx - win.minx) / w;
    double dy = (win.maxy - win.miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double sx = win.minx + (x + (i + 0.5) / ss) * dx;
                    double sy = win.miny + (y + (j + 0.5) / ss) * dy;
                    int best = -1;
                    for (int k = 0; k < g->n; ++k)
                        if (zg_inside(&g->polys[k], sx, sy) &&
                            (best < 0 || g->prio[k] > g->prio[best]))
                            best = k;
                    if (best >= 0) sum += g->colors[best];
                }
            out[y * w + x] = (unsigned char)(sum / (ss * ss));
        }
}

/* 反解 + 合成 + 比对，打印一行报告。返回 zorder_solve 的返回码。 */
static int zg_report(const ZGroup *g, const unsigned char *target, ZBox win,
                     int w, int h, int *order, int *err, double *ms,
                     ZStats *st, int *cyc, int *ncyc) {
    unsigned char out[2048];
    clock_t t0 = clock();
    int rc = zorder_solve(g->polys, g->colors, g->n, target, win, w, h, 4,
                          order, st, cyc, ncyc);
    if (rc == 0) {
        zorder_compose(g->polys, g->colors, g->n, order, win, w, h, 4, out);
        *err = zorder_diff(out, target, w, h);
    } else {
        *err = -1;
    }
    *ms = 1000.0 * (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("%s 顺序[", g->name);
    if (rc == 0)
        for (int k = 0; k < g->n; ++k) printf("%s%d", k ? " " : "", order[k]);
    else
        printf("拒绝");
    printf("]");
    if (rc == 0)
        printf(" 遮挡错像素 %d", *err);
    else
        printf(" 遮挡错像素 -");
    printf(" 耗时 %.3f ms", *ms);
    if (rc == -2) {
        printf(" 环图形[");
        for (int k = 0; k < *ncyc; ++k) printf("%s%d", k ? " " : "", cyc[k]);
        printf("]");
    }
    printf(" (重叠对 %d 零面积 %d 待定 %d)\n", st->pairs, st->zero_area, st->undecided);
    return rc;
}

/* 常规组：参照渲染出目标图后反解。 */
static int zg_run(const ZGroup *g, int *order, int *err, double *ms,
                  ZStats *st, int *cyc, int *ncyc) {
    ZBox win = zg_win(g);
    int w = (int)(2.0 * (win.maxx - win.minx) + 0.5);
    int h = (int)(2.0 * (win.maxy - win.miny) + 0.5);
    static unsigned char target[2048];
    zg_ref_render(g, win, w, h, 4, target);
    return zg_report(g, target, win, w, h, order, err, ms, st, cyc, ncyc);
}

/* 两层：A 添加在先但真值在上层，添加顺序给出错误遮挡。 */
static ZGroup zg_group_two(void) {
    ZGroup g;
    g.name = "两层";
    g.n = 2;
    g.polys[0] = zg_rect(0, 0, 10, 10); g.colors[0] = 60;  g.prio[0] = 2;
    g.polys[1] = zg_rect(5, 5, 15, 15); g.colors[1] = 160; g.prio[1] = 1;
    return g;
}

/* 三层：阶梯三矩形，添加顺序 C,A,B 与真值层序 A<B<C 完全打乱。 */
static ZGroup zg_group_three(void) {
    ZGroup g;
    g.name = "三层";
    g.n = 3;
    g.polys[0] = zg_rect(8, 8, 20, 20); g.colors[0] = 200; g.prio[0] = 3; /* C 最上 */
    g.polys[1] = zg_rect(0, 0, 12, 12); g.colors[1] = 50;  g.prio[1] = 1; /* A 最下 */
    g.polys[2] = zg_rect(4, 4, 16, 16); g.colors[2] = 120; g.prio[2] = 2; /* B 居中 */
    return g;
}

/* 交错：A-B-C-D 链式重叠，层序交错（C 最上、A 次之、B、D），
 * 只有相邻对产生约束，解不是全局层序排序。 */
static ZGroup zg_group_interleaved(void) {
    ZGroup g;
    g.name = "交错";
    g.n = 4;
    g.polys[0] = zg_rect(12, 0, 20, 8); g.colors[0] = 200; g.prio[0] = 4; /* C */
    g.polys[1] = zg_rect(0, 0, 8, 8);   g.colors[1] = 40;  g.prio[1] = 3; /* A */
    g.polys[2] = zg_rect(18, 0, 26, 8); g.colors[2] = 100; g.prio[2] = 1; /* D */
    g.polys[3] = zg_rect(6, 0, 14, 8);  g.colors[3] = 150; g.prio[3] = 2; /* B */
    return g;
}

/* 同框：四个嵌套矩形共享同一中心区域，全部两两重叠。 */
static ZGroup zg_group_frame(void) {
    ZGroup g;
    g.name = "同框";
    g.n = 4;
    g.polys[0] = zg_rect(4, 4, 16, 16); g.colors[0] = 120; g.prio[0] = 2; /* C */
    g.polys[1] = zg_rect(6, 6, 14, 14); g.colors[1] = 210; g.prio[1] = 3; /* D */
    g.polys[2] = zg_rect(0, 0, 20, 20); g.colors[2] = 30;  g.prio[2] = 0; /* A */
    g.polys[3] = zg_rect(2, 2, 18, 18); g.colors[3] = 70;  g.prio[3] = 1; /* B */
    return g;
}

/* 零面积：A-B 共边、B-C 共点，接触面积为零，单独统计不产生约束；
 * D 完全独立作对照。 */
static ZGroup zg_group_zero(void) {
    ZGroup g;
    g.name = "零面积";
    g.n = 4;
    g.polys[0] = zg_rect(0, 0, 5, 8);    g.colors[0] = 60;  g.prio[0] = 1;
    g.polys[1] = zg_rect(5, 0, 13, 8);   g.colors[1] = 120; g.prio[1] = 2;
    g.polys[2] = zg_rect(13, 8, 18, 12); g.colors[2] = 180; g.prio[2] = 3;
    g.polys[3] = zg_rect(20, 0, 24, 4);  g.colors[3] = 220; g.prio[3] = 4;
    return g;
}

/* 环依赖：A 盖 B、B 盖 C、C 盖 A（三形同覆区域任意取 A），
 * 没有任何画家顺序能复现，必须被拒绝。 */
static ZGroup zg_group_cycle(void) {
    ZGroup g;
    g.name = "环依赖";
    g.n = 3;
    g.polys[0] = zg_rect(0, 0, 4, 3); g.colors[0] = 80;  g.prio[0] = 0; /* A */
    g.polys[1] = zg_rect(3, 0, 7, 3); g.colors[1] = 140; g.prio[1] = 0; /* B */
    g.polys[2] = zg_rect(2, 2, 5, 5); g.colors[2] = 200; g.prio[2] = 0; /* C */
    return g;
}

/* 环依赖组的目标图：按循环规则逐子采样定胜者。 */
static void zg_cycle_target(const ZGroup *g, ZBox win, int w, int h, int ss,
                            unsigned char *out) {
    double dx = (win.maxx - win.minx) / w;
    double dy = (win.maxy - win.miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double sx = win.minx + (x + (i + 0.5) / ss) * dx;
                    double sy = win.miny + (y + (j + 0.5) / ss) * dy;
                    int c0 = zg_inside(&g->polys[0], sx, sy);
                    int c1 = zg_inside(&g->polys[1], sx, sy);
                    int c2 = zg_inside(&g->polys[2], sx, sy);
                    int win_k = -1;
                    if (c0 && c1 && c2) win_k = 0;      /* 三形同覆：任意取 A */
                    else if (c0 && c1) win_k = 0;       /* A 盖 B */
                    else if (c1 && c2) win_k = 1;       /* B 盖 C */
                    else if (c0 && c2) win_k = 2;       /* C 盖 A */
                    else if (c0) win_k = 0;
                    else if (c1) win_k = 1;
                    else if (c2) win_k = 2;
                    if (win_k >= 0) sum += g->colors[win_k];
                }
            out[y * w + x] = (unsigned char)(sum / (ss * ss));
        }
}

#endif
