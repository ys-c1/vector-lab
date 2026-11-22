#define _POSIX_C_SOURCE 199309L
#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* 演示用图形：viewBox 中的一个 2:1 矩形带一个方孔。
 * 每条子路径都显式回到起点再起下一段，避免依赖 Z 的闭合语义。 */
static const char *kPath =
    "M20,10 L180,10 L180,90 L20,90 L20,10 "
    "M70,35 L130,35 L130,65 L70,65 L70,35";

/* 手算映射：与库内实现完全独立的 long double 算式。
 *   sx = w / vb.w,  tx = -vb.minx * sx
 *   X(px,py) = vb.minx + (px + 0.5) / sx     （像素中心校正）
 *   Y(px,py) = vb.miny + (py + 0.5) / sy
 */
static void hand_sample(const VBox *vb, int w, int h,
                        int px, int py, long double *ux, long double *uy) {
    long double sx = (long double)w / (long double)vb->w;
    long double sy = (long double)h / (long double)vb->h;
    *ux = (long double)vb->minx + ((long double)px + 0.5L) / sx;
    *uy = (long double)vb->miny + ((long double)py + 0.5L) / sy;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* 参照实现：同一映射，但采样点取像素坐标（左上角）而不做中心校正，
 * 即设备坐标 (px,py) 反算回用户坐标。 */
static void ref_fill_corner(const VPath *p, const VMap *m, int w, int h,
                            unsigned char *buf) {
    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            VPoint s;
            s.x = ((double)x - m->tx) / m->sx;
            s.y = ((double)y - m->ty) / m->sy;
            int cross = 0;
            for (int i = 0; i < p->count; ++i) {
                VSeg g = p->segs[i];
                if ((g.a.y > s.y) == (g.b.y > s.y)) continue;
                double t = (s.y - g.a.y) / (g.b.y - g.a.y);
                double xc = g.a.x + (g.b.x - g.a.x) * t;
                if (xc > s.x) ++cross;
            }
            if (cross & 1) buf[y * w + x] = 255;
        }
    }
}

/* 点到轴对齐矩形“边框”的距离（设备坐标）。点在框内取到四边的最小垂距，
 * 在框外取到矩形的最短欧氏距离。 */
static double dist_rect_perimeter(double x, double y,
                                  double x0, double y0, double x1, double y1) {
    if (x >= x0 && x <= x1 && y >= y0 && y <= y1) {
        double d = x - x0;
        if (x1 - x < d) d = x1 - x;
        if (y - y0 < d) d = y - y0;
        if (y1 - y < d) d = y1 - y;
        return d;
    }
    double ax = fmax(fmax(x0 - x, x - x1), 0.0);
    double ay = fmax(fmax(y0 - y, y - y1), 0.0);
    return sqrt(ax * ax + ay * ay);
}

/* 逐点比对映射表：返回手算与库值的最大绝对差。 */
static long double compare_table(const VBox *vb, int w, int h, int steps,
                                 int verbose) {
    VMap m;
    char ef[SV_FIELD_MAX];
    if (svg_map_fit(vb, w, h, &m, ef) != SV_OK) return -1.0L;
    if (verbose) {
        printf("  像素  手算用户坐标 x        y          库算用户坐标 x       y"
               "           |dx|        |dy|\n");
    }
    long double worst = 0.0L;
    for (int i = 0; i <= steps; ++i) {
        int px = (int)((long long)i * (w - 1) / steps);
        int py = (int)((long long)i * (h - 1) / steps);
        long double hx, hy;
        hand_sample(vb, w, h, px, py, &hx, &hy);
        VPoint q = svg_map_sample(&m, px, py);
        long double dx = fabsl((long double)q.x - hx);
        long double dy = fabsl((long double)q.y - hy);
        if (dx > worst) worst = dx;
        if (dy > worst) worst = dy;
        if (verbose && (i < 4 || i == steps / 2 || i == steps)) {
            printf("  %3d,%3d  %.15Lf  %.15Lf  %.15f  %.15f  %.3Le  %.3Le\n",
                   px, py, hx, hy, q.x, q.y, dx, dy);
        }
    }
    return worst;
}

int main(void) {
    VPath p;
    if (svg_parse(kPath, &p) != 0) { fprintf(stderr, "path parse failed\n"); return 1; }

    VBox vb;
    char ef[SV_FIELD_MAX];
    if (svg_parse_viewbox("0 0 200 100", &vb, ef) != SV_OK) {
        fprintf(stderr, "viewBox parse failed: %s\n", ef);
        return 1;
    }
    printf("viewBox = [%g %g %g %g]，路径 %d 段\n\n",
           vb.minx, vb.miny, vb.w, vb.h, p.count);

    /* ---- 手算映射表逐点比对 ---- */
    printf("== 映射表逐点比对（手算 long double vs 库 svg_map_sample）==\n");
    int ws[2] = {400, 300};
    int hs[2] = {200, 300};
    const char *labels[2] = {"400x200（等比）", "300x300（非等比）"};
    for (int k = 0; k < 2; ++k) {
        int w = ws[k], h = hs[k];
        printf("[%s]\n", labels[k]);
        long double worst = compare_table(&vb, w, h, 8, 1);
        VMap m;
        if (svg_map_fit(&vb, w, h, &m, ef) != SV_OK) return 1;
        printf("  最大差 %.3Le 用户单位；sx=%.9f sy=%.9f tx=%.9f ty=%.9f\n\n",
               worst, m.sx, m.sy, m.tx, m.ty);
    }

    /* ---- 边缘像素偏差上界 ----
     * 采样点做像素中心校正后，与“不校正、取像素坐标”的判定点最多相差
     * 半个像素对角线；因此两种结果的差异只可能出现在距图形边界
     * edge_px 以内的像素上，折回用户单位不超过 edge_user。
     * 方边框特意选在非整像素的用户坐标上（25.1 / 45.1 ...），避免判定点
     * 恰好落在边上产生并列；翻转像素中心到边框的距离应与上界一起打印。 */
    printf("== 边缘像素偏差上界 ==\n");
    VPath ring;
    if (svg_parse("M50,25.1 L150,25.1 L150,74.9 L50,74.9 L50,25.1 "
                  "M90.1,45.1 L109.9,45.1 L109.9,54.9 L90.1,54.9 L90.1,45.1",
                  &ring) != 0) return 1;
    {
        int w = ws[0], h = hs[0];
        VMap m;
        if (svg_map_fit(&vb, w, h, &m, ef) != SV_OK) return 1;
        printf("  edge_px   = %.9f px（半像素对角线 0.5*sqrt(2)）\n", m.edge_px);
        printf("  edge_user = %.9f 用户单位（各轴 0.5/s 合成）\n", m.edge_user);
        unsigned char *on = (unsigned char*)malloc((size_t)w * h);
        unsigned char *ref = (unsigned char*)malloc((size_t)w * h);
        if (svg_fill_mapped(&ring, &vb, w, h, SV_F_VIEWBOX, on, ef) != SV_OK) return 1;
        ref_fill_corner(&ring, &m, w, h, ref);
        double worst = 0.0;
        int flips = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                if (!!on[y * w + x] == !!ref[y * w + x]) continue;
                ++flips;
                /* 设备坐标下的外框 [100,300]x[50.2,149.8]、
                 * 内孔 [180.2,219.8]x[90.2,109.8]。 */
                double cx = x + 0.5, cy = y + 0.5;
                double d = dist_rect_perimeter(cx, cy, 100.0, 50.2, 300.0, 149.8);
                double d2 = dist_rect_perimeter(cx, cy, 180.2, 90.2, 219.8, 109.8);
                if (d2 < d) d = d2;
                if (d > worst) worst = d;
            }
        }
        printf("  两种采样方式翻转的像素 %d 个，翻转像素中心到边框最远距离 "
               "%.9f px（上界 %.9f）\n", flips, worst, m.edge_px);
        printf("  折回用户单位 %.9f（上界 edge_user %.9f）\n",
               worst / m.sx, m.edge_user);
        free(on); free(ref);
    }

    /* ---- 关闭开关：逐像素相同 ---- */
    printf("\n== 关闭换算（flags=0）与 svg_fill 逐像素比对 ==\n");
    for (int k = 0; k < 2; ++k) {
        int w = ws[k], h = hs[k];
        unsigned char *a = (unsigned char*)malloc((size_t)w * h);
        unsigned char *b = (unsigned char*)malloc((size_t)w * h);
        svg_fill(&p, w, h, a);
        svg_fill_mapped(&p, &vb, w, h, 0, b, ef);
        size_t diff = 0;
        for (size_t i = 0; i < (size_t)w * h; ++i) if (a[i] != b[i]) ++diff;
        printf("  %-18s 不同像素 %zu 个（应为 0）\n", labels[k], diff);
        free(a); free(b);
    }

    /* ---- 两档尺寸 + 非法输入：字段名 / 宽高比误差 / 耗时各一栏 ---- */
    printf("\n== 两档尺寸与非法输入：字段名 / 宽高比误差 / 耗时 ==\n");
    printf("%-22s %-14s %14s %14s\n",
           "场景", "字段名", "宽高比误差", "耗时(ms/次)");
    for (int k = 0; k < 2; ++k) {
        int w = ws[k], h = hs[k];
        VMap m;
        if (svg_map_fit(&vb, w, h, &m, ef) != SV_OK) return 1;
        unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
        const int reps = 50;
        double t0 = now_ms();
        for (int r = 0; r < reps; ++r)
            svg_fill_mapped(&p, &vb, w, h, SV_F_VIEWBOX, buf, ef);
        double ms = (now_ms() - t0) / reps;
        printf("%-22s %-14s %14.9f %14.3f\n", labels[k], "-", m.aspect_err, ms);
        free(buf);
    }

    struct {
        const char *name;
        VBox vb;
        int w, h;
    } cases[] = {
        {"viewBox.w = 0",      {0, 0, 0, 100},   200, 100},
        {"viewBox.h = 0",      {0, 0, 100, 0},   200, 100},
        {"viewBox.w < 0",      {0, 0, -5, 100},  200, 100},
        {"目标宽 = 0",         {0, 0, 100, 100}, 0,   100},
        {"目标高 < 0",         {0, 0, 100, 100}, 200, -1},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
        VMap m;
        int rc = svg_map_fit(&cases[i].vb, cases[i].w, cases[i].h, &m, ef);
        (void)rc;
        printf("%-22s %-14s %14s %14s\n", cases[i].name, ef, "-", "-");
    }
    /* 解析阶段的字段名 */
    {
        VBox t;
        int rc = svg_parse_viewbox("0 0 100", &t, ef);
        (void)rc;
        printf("%-22s %-14s %14s %14s\n", "解析 \"0 0 100\"", ef, "-", "-");
        rc = svg_parse_viewbox("0 0 nan 10", &t, ef);
        (void)rc;
        printf("%-22s %-14s %14s %14s\n", "解析 \"0 0 nan 10\"", ef, "-", "-");
    }

    /* ---- PGM 导出：两档尺寸 ---- */
    for (int k = 0; k < 2; ++k) {
        int w = ws[k], h = hs[k];
        unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
        if (svg_fill_mapped(&p, &vb, w, h, SV_F_VIEWBOX, buf, ef) != SV_OK) return 1;
        char path[64];
        snprintf(path, sizeof path, "build/demo_%dx%d.pgm", w, h);
        if (svg_save_pgm(path, buf, w, h) != 0) return 1;
        printf("\n已导出 %s\n", path);
        free(buf);
    }
    return 0;
}
