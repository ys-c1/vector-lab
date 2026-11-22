/* src/winrot.c */
#include "winrot.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int winrot_solve(double bw, double bh, double win_w, double win_h,
                 double cap_deg, WinRotSolve *out);

/* 零旋转窗口必须逐像素等于最朴素的逐像素中心采样：窗口 = 包围盒，采样点
 * (x+0.5, y+0.5) 直接进多边形，不经过旋转变换。返回 0 表示完全相同。 */
static int zero_rot_matches_plain(const RScene *s, RBox bb, int w, int h) {
    unsigned char *a = (unsigned char *)calloc((size_t)w * h, 1);
    unsigned char *b = (unsigned char *)calloc((size_t)w * h, 1);
    if (!a || !b) { free(a); free(b); return 0; }
    double bw = bb.maxx - bb.minx, bh = bb.maxy - bb.miny;
    double cx = (bb.minx + bb.maxx) * 0.5, cy = (bb.miny + bb.maxy) * 0.5;
    if (winrot_render(s, cx, cy, bw, bh, 0.0, w, h, 1, a) != 0) {
        free(a); free(b); return 0;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double px = bb.minx + (x + 0.5) * bw / w;
            double py = bb.miny + (y + 0.5) * bh / h;
            int hit = 0;
            for (int k = 0; k < s->npolys && !hit; ++k) {
                const RPoly *p = &s->polys[k];
                int c = 0;
                for (int i = 0; i < p->n; ++i) {
                    RPoint u = p->pts[i], v = p->pts[(i + 1) % p->n];
                    if ((u.y > py) == (v.y > py)) continue;
                    double t = (py - u.y) / (v.y - u.y);
                    if (u.x + (v.x - u.x) * t > px) ++c;
                }
                hit = c & 1;
            }
            b[y * w + x] = (unsigned char)(hit ? 255 : 0);
        }
    int same = memcmp(a, b, (size_t)w * h) == 0;
    free(a); free(b);
    return same;
}

int main(void) {
    int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

    WinRotSolve r;

    /* 1. 解析解：300x150 盒、200x100 窗，高度约束先卡住 -> 15.565363... deg */
    CK(winrot_solve(300, 150, 200, 100, 90.0, &r) == WINROT_OK);
    CK(fabs(r.theta_deg - 15.565363584288615) < 1e-9);
    CK(r.binding == WINROT_BIND_HEIGHT);
    /* 另一条约束在零旋转就放得下，允许到 90 度，不起作用 */
    CK(fabs(r.theta_width_deg - 90.0) < 1e-12);

    /* 2. 零旋转就能放下但有旋转余量：240x160 盒、200x120 窗 */
    CK(winrot_solve(240, 160, 200, 120, 45.0, &r) == WINROT_OK);
    CK(fabs(r.theta_deg - 12.350100126209533) < 1e-9);

    /* 3. 超过 45 度上限：210x192 盒、200x60 窗 -> 50.155779 deg */
    CK(winrot_solve(210, 192, 200, 60, 45.0, &r) == WINROT_OVER_CAP);
    CK(fabs(r.theta_deg - 50.155779235625374) < 1e-9);

    /* 4. 零旋转都放不下 */
    CK(winrot_solve(100, 100, 120, 80, 45.0, &r) == WINROT_NOFIT);
    CK(winrot_solve(100, 100, 80, 120, 45.0, &r) == WINROT_NOFIT);

    /* 5. 退化包围盒单独报 */
    CK(winrot_solve(0, 0, 10, 10, 45.0, &r) == WINROT_DEGEN_POINT);
    CK(winrot_solve(0, 50, 10, 10, 45.0, &r) == WINROT_DEGEN_LINE);
    CK(winrot_solve(50, 0, 10, 10, 45.0, &r) == WINROT_DEGEN_LINE);

    /* 6. 参数非法 */
    CK(winrot_solve(10, 10, 0, 5, 45.0, &r) == WINROT_BADARG);
    CK(winrot_solve(-1, 10, 5, 5, 45.0, &r) == WINROT_BADARG);
    CK(winrot_solve(10, 10, 5, 5, 0.0, &r) == WINROT_BADARG);
    CK(winrot_solve(10, 10, 5, 5, 91.0, &r) == WINROT_BADARG);

    /* 7. 窗口等于包围盒：两条曲线都恰好在该盒的右上角相切，最大角 0 */
    CK(winrot_solve(100, 80, 100, 80, 45.0, &r) == WINROT_OK);
    CK(fabs(r.theta_deg) < 1e-12);
    CK(r.theta_width_deg == INFINITY || fabs(r.theta_width_deg) < 1e-12);
    CK(r.theta_height_deg == INFINITY || fabs(r.theta_height_deg) < 1e-12);

    /* 8. 正方形里放扁窗：两条约束都真卡住，宽窗卡宽度、高窗卡高度 */
    CK(winrot_solve(100, 100, 95, 50, 90.0, &r) == WINROT_OK);
    CK(fabs(r.theta_deg - 6.42727476510081) < 1e-9);
    CK(r.binding == WINROT_BIND_WIDTH);
    CK(winrot_solve(100, 100, 50, 95, 90.0, &r) == WINROT_OK);
    CK(fabs(r.theta_deg - 6.42727476510081) < 1e-9);
    CK(r.binding == WINROT_BIND_HEIGHT);
    /* 两条都放得下：最大角封顶 90 度，无约束 */
    CK(winrot_solve(400, 400, 10, 10, 90.0, &r) == WINROT_OK);
    CK(fabs(r.theta_deg - 90.0) < 1e-12);
    CK(r.binding == WINROT_BIND_NONE);

    /* 9. 旋转面一致性：解出的角度左右两侧空角为零/非零 */
    RPoint sq[4] = {{0, 0}, {240, 0}, {240, 160}, {0, 160}};
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    RBox bb = {0, 0, 240, 160};
    unsigned char buf[240 * 160];
    const int W = 240, H = 160;
    CK(winrot_solve(240, 160, 200, 120, 45.0, &r) == WINROT_OK);
    CK(winrot_render(&s, 120, 80, 200, 120, r.theta_deg, W, H, 4, buf) == 0);
    CK(winrot_count_corner_empty(buf, W, H) == 0);
    CK(winrot_count_empty(buf, W, H) == 0);
    CK(winrot_render(&s, 120, 80, 200, 120, r.theta_deg + 3.0, W, H, 4, buf) == 0);
    CK(winrot_count_corner_empty(buf, W, H) > 0);

    /* 10. 零旋转与朴素逐像素采样逐字节一致 */
    CK(zero_rot_matches_plain(&s, bb, W, H));
    RPoint tri[3] = {{0, 0}, {300, 0}, {300, 150}}, tri2[3] = {{0, 0}, {300, 150}, {0, 150}};
    RScene s2;
    scene_init(&s2);
    CK(scene_add_poly(&s2, tri, 3) == 0);
    CK(scene_add_poly(&s2, tri2, 3) == 0);
    RBox bb2 = {0, 0, 300, 150};
    CK(zero_rot_matches_plain(&s2, bb2, 300, 150));

    /* 11. 角块尺寸：随短边缩放，短边小于 8 时退化为 1 */
    CK(winrot_corner_box(240, 160) == 20);
    CK(winrot_corner_box(8, 8) == 1);
    CK(winrot_corner_box(4, 4) == 1);

    /* 12. 计数接口的坏参数 */
    CK(winrot_count_corner_empty((const unsigned char *)buf, 0, 10) == -1);
    CK(winrot_count_empty((const unsigned char *)buf, 10, 0) == -1);
    CK(winrot_render(&s, 120, 80, 200, 120, 0.0, W, H, 9, buf) == -1);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
