#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int count_on(const unsigned char *b, int n) {
    int on = 0;
    for (int i = 0; i < n; ++i) if (b[i]) ++on;
    return on;
}

/* 独立参照实现：同一映射，但采样点不校正，直接取像素坐标 (px,py)。 */
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

int main(void) {
    VPath p;
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &p) == 0);
    CK(p.count == 3);
    unsigned char *buf = (unsigned char*)malloc(20 * 20);
    CK(svg_fill(&p, 20, 20, buf) == 0);
    int on = 0;
    for (int i = 0; i < 400; ++i) if (buf[i]) ++on;
    CK(on > 40 && on < 60);
    CK(svg_parse("L0,0", &p) == -2);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == -3);
    CK(svg_parse("M0,0 L", &p) == -4);

    /* --- viewBox 解析与字段报错 --- */
    char ef[SV_FIELD_MAX];
    VBox vb;
    CK(svg_parse_viewbox("0 0 100 50", &vb, ef) == SV_OK);
    CK(vb.minx == 0 && vb.miny == 0 && vb.w == 100 && vb.h == 50);
    CK(svg_parse_viewbox("-2,-3,40,20", &vb, ef) == SV_OK);
    CK(vb.minx == -2 && vb.miny == -3 && vb.w == 40 && vb.h == 20);
    CK(svg_parse_viewbox("0 0 100", &vb, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox.h") == 0);
    CK(svg_parse_viewbox("0 0 1 2 3", &vb, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox") == 0);
    CK(svg_parse_viewbox("0 0 nan 5", &vb, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox.w") == 0);
    CK(svg_parse_viewbox(NULL, &vb, ef) == SV_E_ARG);
    CK(strcmp(ef, "viewBox") == 0);

    /* --- 映射公式：缩放 + 偏移 --- */
    VBox vb2 = {10, 20, 100, 50};
    VMap m;
    CK(svg_map_fit(&vb2, 200, 100, &m, ef) == SV_OK);
    CK(m.sx == 2.0 && m.sy == 2.0);
    CK(m.tx == -20.0 && m.ty == -40.0);
    VPoint q = svg_map_apply(&m, (VPoint){10, 20});
    CK(q.x == 0.0 && q.y == 0.0);
    q = svg_map_apply(&m, (VPoint){110, 70});
    CK(q.x == 200.0 && q.y == 100.0);
    /* 像素中心校正：像素 (0,0) 的判定点落在设备坐标 (0.5,0.5)，反算回
     * 用户坐标应是 minx + 0.5/sx = 10.25, miny + 0.5/sy = 20.25。 */
    q = svg_map_sample(&m, 0, 0);
    CK(fabs(q.x - 10.25) < 1e-12 && fabs(q.y - 20.25) < 1e-12);
    q = svg_map_sample(&m, 199, 99);
    CK(fabs(q.x - 109.75) < 1e-12 && fabs(q.y - 69.75) < 1e-12);
    for (int px = 0; px < 200; px += 17) {
        for (int py = 0; py < 100; py += 13) {
            VPoint u = svg_map_sample(&m, px, py);
            VPoint d = svg_map_apply(&m, u);
            CK(fabs(d.x - (px + 0.5)) < 1e-9 && fabs(d.y - (py + 0.5)) < 1e-9);
        }
    }
    CK(fabs(m.aspect_err) < 1e-15);
    CK(fabs(m.edge_px - 0.5 * sqrt(2.0)) < 1e-15);
    CK(fabs(m.edge_user - sqrt(2.0 * (0.25 / 4.0))) < 1e-15);

    /* --- 非等比缩放：宽高比误差 = |sx/sy - 1| --- */
    VBox vb3 = {0, 0, 100, 100};
    CK(svg_map_fit(&vb3, 200, 100, &m, ef) == SV_OK);
    CK(m.sx == 2.0 && m.sy == 1.0);
    CK(fabs(m.aspect_err - 1.0) < 1e-15);
    CK(fabs(m.edge_user - sqrt(0.25 + 0.0625)) < 1e-15);

    /* --- 字段报错：viewBox 为零 / 目标尺寸为负 --- */
    VBox vzero = {0, 0, 0, 50};
    CK(svg_map_fit(&vzero, 100, 100, &m, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox.w") == 0);
    VBox vhz = {0, 0, 10, 0};
    CK(svg_map_fit(&vhz, 100, 100, &m, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox.h") == 0);
    VBox vneg = {0, 0, -5, 10};
    CK(svg_map_fit(&vneg, 100, 100, &m, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox.w") == 0);
    CK(svg_map_fit(&vb3, -1, 100, &m, ef) == SV_E_TARGET);
    CK(strcmp(ef, "target.w") == 0);
    CK(svg_map_fit(&vb3, 100, -1, &m, ef) == SV_E_TARGET);
    CK(strcmp(ef, "target.h") == 0);
    CK(svg_map_fit(&vb3, 0, 100, &m, ef) == SV_E_TARGET);
    CK(strcmp(ef, "target.w") == 0);
    VBox vnan = {NAN, 0, 10, 10};
    CK(svg_map_fit(&vnan, 100, 100, &m, ef) == SV_E_VIEWBOX);
    CK(strcmp(ef, "viewBox.minx") == 0);
    CK(svg_map_fit(NULL, 100, 100, &m, ef) == SV_E_ARG);
    CK(strcmp(ef, "viewBox") == 0);

    /* --- 关闭开关：与 svg_fill 逐像素相同 --- */
    VPath sq;
    CK(svg_parse("M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z", &sq) == 0);
    unsigned char *a = (unsigned char*)malloc(50 * 50);
    unsigned char *b = (unsigned char*)malloc(50 * 50);
    CK(svg_fill(&sq, 50, 50, a) == 0);
    VBox vbunused = {0, 0, 1, 1};
    CK(svg_fill_mapped(&sq, &vbunused, 50, 50, 0, b, ef) == SV_OK);
    CK(memcmp(a, b, 50 * 50) == 0);
    CK(svg_fill_mapped(&sq, NULL, 50, 50, 0, b, ef) == SV_OK);
    CK(memcmp(a, b, 50 * 50) == 0);
    CK(svg_fill_mapped(&sq, &vbunused, 50, 50, 8u, b, ef) == SV_E_FLAGS);
    CK(strcmp(ef, "flags") == 0);

    /* --- 打开开关：viewBox 10..40 映射到 30x30 应与直接按用户坐标
     *     画在 30x30 上的结果一致（sx=sy=1, tx=ty=-10，采样点同为
     *     x+0.5，故逐像素相同）。 --- */
    VPath s2;
    CK(svg_parse("M10,10 L40,10 L40,40 L10,40 L10,10 "
                 "M20,20 L30,20 L30,30 L20,30 L20,20", &s2) == 0);
    VPath s3;
    CK(svg_parse("M0,0 L30,0 L30,30 L0,30 L0,0 "
                 "M10,10 L20,10 L20,20 L10,20 L10,10", &s3) == 0);
    CK(svg_fill(&s3, 30, 30, a) == 0);
    VBox v30 = {10, 10, 30, 30};
    CK(svg_fill_mapped(&s2, &v30, 30, 30, SV_F_VIEWBOX, b, ef) == SV_OK);
    CK(memcmp(a, b, 30 * 30) == 0);
    CK(count_on(b, 30 * 30) == 900 - 100);

    /* 2 倍放大：viewBox 0..10 映射到 20x20，等价于把路径放大 2 倍
     * 后直接光栅化（像素中心同为 x+0.5 的 2 倍网格）。 */
    VPath s4, s5;
    CK(svg_parse("M1,1 L9,1 L9,9 L1,9 Z", &s4) == 0);
    CK(svg_parse("M2,2 L18,2 L18,18 L2,18 Z", &s5) == 0);
    unsigned char *a20 = (unsigned char*)malloc(20 * 20);
    unsigned char *b20 = (unsigned char*)malloc(20 * 20);
    CK(svg_fill(&s5, 20, 20, a20) == 0);
    VBox v10 = {0, 0, 10, 10};
    CK(svg_fill_mapped(&s4, &v10, 20, 20, SV_F_VIEWBOX, b20, ef) == SV_OK);
    CK(memcmp(a20, b20, 20 * 20) == 0);
    CK(count_on(b20, 20 * 20) == 256);

    /* --- 边缘像素偏差上界 ---
     * 参照实现不做像素中心校正，采样点取像素坐标（左上角）。同一像素
     * 两种采样点最多相差半像素对角线，于是凡结论不同的像素，其像素
     * 中心到图形边界的距离必不超过 edge_px；折回用户单位不超过
     * edge_user。取 viewBox [0,8]^2 映射到 20x20（sx=sy=2.5），用户方框
     * [0.5,7.5]^2 的设备边界在 1.25 / 18.75，落在像素坐标与像素中心
     * 之间，左右两侧必然出现归属翻转的像素。 */
    VPath s6;
    CK(svg_parse("M0.5,0.5 L7.5,0.5 L7.5,7.5 L0.5,7.5 Z", &s6) == 0);
    {
        VMap m2;
        VBox v8 = {0, 0, 8, 8};
        CK(svg_map_fit(&v8, 20, 20, &m2, ef) == SV_OK);
        unsigned char *cor = (unsigned char*)malloc(20 * 20);
        unsigned char *cen = (unsigned char*)malloc(20 * 20);
        ref_fill_corner(&s6, &m2, 20, 20, cor);
        CK(svg_fill_mapped(&s6, &v8, 20, 20, SV_F_VIEWBOX, cen, ef) == SV_OK);
        int flips = 0;
        for (int py = 0; py < 20; ++py) {
            for (int px = 0; px < 20; ++px) {
                int a_on = cen[py * 20 + px] != 0;
                int b_on = cor[py * 20 + px] != 0;
                if (a_on == b_on) continue;
                ++flips;
                /* 设备边界 [1.25, 18.75]^2 */
                double cx = px + 0.5, cy = py + 0.5;
                double ax = fmax(fmax(1.25 - cx, cx - 18.75), 0.0);
                double ay = fmax(fmax(1.25 - cy, cy - 18.75), 0.0);
                double dist = sqrt(ax * ax + ay * ay);
                if (ax == 0.0) dist = ay;
                if (ay == 0.0) dist = ax;
                CK(dist <= m2.edge_px + 1e-12);
                /* 折回用户单位后同样不超过 edge_user */
                CK(dist / 2.5 <= m2.edge_user + 1e-12);
            }
        }
        CK(flips > 0);
        free(cor); free(cen);
    }

    /* 采样点校正本身：像素中心经由映射后必须回到同一个设备坐标。 */
    for (int px = 0; px < 20; px += 7) {
        for (int py = 0; py < 20; py += 5) {
            VPoint d = svg_map_apply(&m, svg_map_sample(&m, px, py));
            CK(fabs(d.x - (px + 0.5)) < 1e-9);
            CK(fabs(d.y - (py + 0.5)) < 1e-9);
        }
    }

    /* --- 带 Z 的子路径会多出一条“当前点 -> 起点”的闭合边（现状语义，
     *     与 viewBox 换算无关）。这里改写成显式回到起点，确认两者在
     *     恒等映射下逐像素一致。 --- */
    {
        VPath zz, zp;
        CK(svg_parse("M2,2 L8,2 L8,8 L2,8 Z", &zz) == 0);
        CK(svg_parse("M2,2 L8,2 L8,8 L2,8 L2,2", &zp) == 0);
        unsigned char *za = (unsigned char*)malloc(10 * 10);
        unsigned char *zb = (unsigned char*)malloc(10 * 10);
        CK(svg_fill(&zz, 10, 10, za) == 0);
        VBox vz = {0, 0, 10, 10};   /* 恒等映射：sx=sy=1, tx=ty=0 */
        CK(svg_fill_mapped(&zp, &vz, 10, 10, SV_F_VIEWBOX, zb, ef) == SV_OK);
        CK(count_on(za, 100) == 36);
        CK(memcmp(za, zb, 100) == 0);
        free(za); free(zb);
    }

    free(a); free(b); free(a20); free(b20); free(buf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
