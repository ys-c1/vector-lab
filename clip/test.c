#include "clip.h"
#include "order.h"
#include "groups.h"
#include "golden.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 与改动前生成的黄金参照保持同一格式：rc count，随后每行一个顶点 */
static int serialize(char *buf, size_t cap, const CPoint *poly, int n,
                     const CPoint *win, int wn) {
    CPoly out;
    int rc = clip_convex(poly, n, win, wn, &out);
    int off = snprintf(buf, cap, "%d %d\n", rc, rc == 0 ? out.count : -1);
    if (rc == 0)
        for (int i = 0; i < out.count; ++i)
            off += snprintf(buf + off, cap - (size_t)off, "%.17g %.17g\n",
                            out.pts[i].x, out.pts[i].y);
    return off;
}

static int fixture(int k, CPoint *poly, CPoint *win, int *wn) {
    switch (k) {
    case 0: {
        CPoint p[4] = {{0,0},{20,0},{20,10},{0,10}};
        CPoint w[4] = {{5,-5},{15,-5},{15,5},{5,5}};
        memcpy(poly, p, sizeof p); memcpy(win, w, sizeof w); *wn = 4; return 4;
    }
    case 1: {
        CPoint p[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint w[4] = {{2,2},{8,2},{8,8},{2,8}};
        memcpy(poly, p, sizeof p); memcpy(win, w, sizeof w); *wn = 4; return 4;
    }
    case 2: {
        CPoint p[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
        CPoint w[4] = {{2,2},{8,2},{8,8},{2,8}};
        memcpy(poly, p, sizeof p); memcpy(win, w, sizeof w); *wn = 4; return 4;
    }
    case 3: case 4: case 5: case 6:
        group_win(k - 3, win); *wn = 4;
        return group_poly(k - 3, poly);
    default: {
        CPoint q[10] = {{0,8},{2,2},{8,2},{3,-2},{5,-8},{0,-4},{-5,-8},{-3,-2},{-8,2},{-2,2}};
        CPoint w[5] = {{-4,-4},{4,-4},{6,2},{0,6},{-6,2}};
        memcpy(poly, q, sizeof q); memcpy(win, w, sizeof w); *wn = 5; return 10;
    }
    }
}

int main(void) {
    /* 原有检查：行为必须保持不变 */
    {
        CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
        CPoly out;
        CK(clip_convex(poly,4,win,4,&out) == 0);
        CK(out.count == 4);
        CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);
        CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
        CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);
        CK(clip_convex(poly,2,win,4,&out) == -1);
    }

    /* 逐字节黄金参照：重构后 clip_convex 输出与改动前完全一致 */
    {
        const char *golds[8] = { GOLD_F0, GOLD_F1, GOLD_F2, GOLD_F3,
                                 GOLD_F4, GOLD_F5, GOLD_F6, GOLD_F7 };
        char buf[8192];
        for (int k = 0; k < 8; ++k) {
            CPoint poly[64], win[8]; int wn;
            int n = fixture(k, poly, win, &wn);
            serialize(buf, sizeof buf, poly, n, win, wn);
            CK(strcmp(buf, golds[k]) == 0);
        }
    }

    /* clip_convex_ordered(NULL) 与 clip_convex 逐字节一致 */
    {
        for (int k = 0; k < 8; ++k) {
            CPoint poly[64], win[8]; int wn;
            int n = fixture(k, poly, win, &wn);
            CPoly a, b;
            CK(clip_convex(poly, n, win, wn, &a) == 0);
            CK(clip_convex_ordered(poly, n, win, wn, NULL, &b, NULL) == 0);
            CK(a.count == b.count);
            CK(memcmp(a.pts, b.pts, (size_t)a.count * sizeof(CPoint)) == 0);
        }
    }

    /* 四组窗口：最优顺序顶点数比默认顺序少三成以上，且锁定具体数值 */
    {
        static const int expect_best[4] = {12, 12, 13, 12};
        static const int expect_def[4]  = {20, 21, 23, 20};
        for (int g = 0; g < 4; ++g) {
            CPoint poly[64], win[4];
            int n = group_poly(g, poly);
            group_win(g, win);
            CPoly out; int ord[4];
            CK(clip_convex(poly, n, win, 4, &out) == 0);
            int def = out.count;
            CK(def == expect_def[g]);
            CK(clip_best_order(poly, n, win, 4, ord, &out) == 0);
            CK(out.count == expect_best[g]);
            CK(out.count * 10 <= def * 7); /* 少三成 */
        }
    }

    /* 顶点数相同按边号定序：正方形套正方形，24 种顺序同为 4，取 0>1>2>3 */
    {
        CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
        CPoly out; int ord[4] = {9,9,9,9};
        CK(clip_best_order(poly, 4, win, 4, ord, &out) == 0);
        CK(out.count == 4);
        CK(ord[0] == 0 && ord[1] == 1 && ord[2] == 2 && ord[3] == 3);
    }

    /* 暴力校验：枚举结果必须等于全排列最小值，且并列时取边号字典序最小 */
    {
        CPoint poly[64], win[4];
        int n = group_poly(0, poly);
        group_win(0, win);
        CPoly out; int ord[4];
        CK(clip_best_order(poly, n, win, 4, ord, &out) == 0);
        int perm[4] = {0,1,2,3}, minc = 1 << 30, minlex[4] = {0,0,0,0}, first = 1;
        for (;;) {
            CPoly cur;
            CK(clip_convex_ordered(poly, n, win, 4, perm, &cur, NULL) == 0);
            if (cur.count < minc) {
                minc = cur.count;
                memcpy(minlex, perm, sizeof perm);
                first = 0;
            } else if (cur.count == minc && first) {
                memcpy(minlex, perm, sizeof perm);
            }
            (void)first;
            /* 字典序下一个排列 */
            int i = 2;
            while (i >= 0 && perm[i] > perm[i+1]) --i;
            if (i < 0) break;
            int j = 3;
            while (perm[j] < perm[i]) --j;
            int t = perm[i]; perm[i] = perm[j]; perm[j] = t;
            for (int l = i+1, r = 3; l < r; ++l, --r) { t = perm[l]; perm[l] = perm[r]; perm[r] = t; }
        }
        CK(out.count == minc);
        CK(memcmp(ord, minlex, sizeof ord) == 0);
    }

    /* 窗口为空或边重合要拒 */
    {
        CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
        CPoint ok[4] = {{2,2},{8,2},{8,8},{2,8}};
        CPoint col[3] = {{0,0},{5,0},{10,0}};                 /* 面积为零 */
        CPoint zlen[4] = {{0,0},{10,0},{10,0},{0,10}};        /* 零长边 */
        CPoint coll[5] = {{0,0},{5,0},{10,0},{10,10},{0,10}}; /* 共线邻边 */
        CPoly out; int ord[9];
        CK(clip_check_window(ok, 4) == CLIP_ORD_OK);
        CK(clip_check_window(NULL, 0) == CLIP_ORD_EMPTY);
        CK(clip_check_window(ok, 2) == CLIP_ORD_EMPTY);
        CK(clip_check_window(col, 3) == CLIP_ORD_EMPTY);
        CK(clip_check_window(zlen, 4) == CLIP_ORD_COINCIDENT);
        CK(clip_check_window(coll, 5) == CLIP_ORD_COINCIDENT);
        CK(clip_best_order(poly, 4, col, 3, ord, &out) == CLIP_ORD_EMPTY);
        CK(clip_best_order(poly, 4, zlen, 4, ord, &out) == CLIP_ORD_COINCIDENT);
        CK(clip_best_order(poly, 4, coll, 5, ord, &out) == CLIP_ORD_COINCIDENT);
        CK(clip_order_err(CLIP_ORD_EMPTY) != NULL);
        CK(clip_order_err(CLIP_ORD_COINCIDENT) != NULL);
        /* 边数超过枚举上限要拒 */
        CPoint many[9];
        for (int i = 0; i < 9; ++i) {
            many[i].x = 10 + 5 * cos(2 * M_PI * i / 9);
            many[i].y = 10 + 5 * sin(2 * M_PI * i / 9);
        }
        CK(clip_check_window(many, 9) == CLIP_ORD_OK);
        CK(clip_best_order(poly, 4, many, 9, ord, &out) == CLIP_ORD_TOOMANY);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
