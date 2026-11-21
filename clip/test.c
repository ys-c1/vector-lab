#include "clip.h"
#include <stdio.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 逐边手算的参照实现：每条边把两个端点沿内法向平移 dist，
   再用参数式求相邻平移边的交点。与 clip_shrink 的线交式互独立。 */
static void ref_shrink(const CPoly *in, double d, CPoly *out) {
    int n = in->count;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = in->pts[i], b = in->pts[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    double sg = s >= 0.0 ? 1.0 : -1.0;
    CPoint e0[CLIP_MAX], e1[CLIP_MAX];
    for (int i = 0; i < n; ++i) {
        CPoint a = in->pts[i], b = in->pts[(i + 1) % n];
        double dx = b.x - a.x, dy = b.y - a.y, L = hypot(dx, dy);
        double nx = -dy / L * sg, ny = dx / L * sg;
        e0[i].x = a.x + d * nx; e0[i].y = a.y + d * ny;
        e1[i].x = b.x + d * nx; e1[i].y = b.y + d * ny;
    }
    out->count = n;
    for (int i = 0; i < n; ++i) {
        int h = (i + n - 1) % n;
        double d1x = e1[h].x - e0[h].x, d1y = e1[h].y - e0[h].y;
        double d2x = e1[i].x - e0[i].x, d2y = e1[i].y - e0[i].y;
        double wx = e0[i].x - e0[h].x, wy = e0[i].y - e0[h].y;
        double t = (wx * d2y - wy * d2x) / (d1x * d2y - d1y * d2x);
        out->pts[i].x = e0[h].x + t * d1x;
        out->pts[i].y = e0[h].y + t * d1y;
    }
}

static void load(CPoly *p, const CPoint *pts, int n) {
    p->count = n;
    for (int i = 0; i < n; ++i) p->pts[i] = pts[i];
}

static void ring_eq(const CPoly *a, const CPoly *b, double eps) {
    CK(a->count == b->count);
    if (a->count != b->count) return;
    for (int i = 0; i < a->count; ++i) {
        CK(fabs(a->pts[i].x - b->pts[i].x) < eps);
        CK(fabs(a->pts[i].y - b->pts[i].y) < eps);
    }
}

int main(void) {
    CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
    CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
    CPoly out;
    CK(clip_convex(poly,4,win,4,&out) == 0);
    CK(out.count == 4);
    CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);
    CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
    CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);
    CK(clip_convex(poly,2,win,4,&out) == -1);

    /* ---- 收缩：矩形手算核对 ---- */
    CPoly rin, sh, id;
    CPoint rectp[4] = {{0,0},{10,0},{10,6},{0,6}};
    load(&rin, rectp, 4);
    int ee = -1;
    CK(clip_shrink(&rin, 1.0, &sh, &ee) == CLIP_SHRINK_OK);
    CK(sh.count == 4);
    CK(fabs(sh.pts[0].x - 1.0) < 1e-9 && fabs(sh.pts[0].y - 1.0) < 1e-9);
    CK(fabs(sh.pts[1].x - 9.0) < 1e-9 && fabs(sh.pts[1].y - 1.0) < 1e-9);
    CK(fabs(sh.pts[2].x - 9.0) < 1e-9 && fabs(sh.pts[2].y - 5.0) < 1e-9);
    CK(fabs(sh.pts[3].x - 1.0) < 1e-9 && fabs(sh.pts[3].y - 5.0) < 1e-9);
    CK(fabs(clip_area(&sh) - 32.0) < 1e-9);

    /* 偏移关掉：结果回到当前版本（原样复制） */
    CK(clip_shrink(&rin, 0.0, &id, NULL) == CLIP_SHRINK_OK);
    ring_eq(&id, &rin, 1e-12);

    /* 偏移量为负要报错 */
    CK(clip_shrink(&rin, -0.5, &sh, &ee) == CLIP_SHRINK_ENEG);

    /* 收缩过猛顶穿对边：报错并指出边号 */
    ee = -1;
    CK(clip_shrink(&rin, 4.0, &sh, &ee) == CLIP_SHRINK_SELFX);
    CK(ee >= 0 && ee < 4);

    /* 与逐边手算参照核对：不规则凸六边形，两个绕向各一遍 */
    CPoly hin, hrev, got, ref;
    CPoint hexp[6] = {{0,0},{7,-1},{12,3},{10,9},{4,11},{-1,6}};
    load(&hin, hexp, 6);
    CK(clip_shrink(&hin, 0.75, &got, NULL) == CLIP_SHRINK_OK);
    ref_shrink(&hin, 0.75, &ref);
    ring_eq(&got, &ref, 1e-9);
    hrev.count = 6;
    for (int i = 0; i < 6; ++i) hrev.pts[i] = hin.pts[5 - i];
    CK(clip_shrink(&hrev, 0.75, &got, NULL) == CLIP_SHRINK_OK);
    ref_shrink(&hrev, 0.75, &ref);
    ring_eq(&got, &ref, 1e-9);

    /* 裁剪 + 收缩流水线：clip 输出再收缩，窗口即手算答案 */
    CPoly clipped;
    CK(clip_convex(poly, 4, win, 4, &clipped) == 0);
    CK(clip_shrink(&clipped, 1.0, &sh, &ee) == CLIP_SHRINK_OK);
    CK(sh.count == 4);
    CK(fabs(clip_area(&sh) - 16.0) < 1e-9); /* [3,7]x[3,7] */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
