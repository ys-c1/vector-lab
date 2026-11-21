#include "lamp.h"
#include <math.h>
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static double dist(LPoint a, LPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return sqrt(dx * dx + dy * dy);
}

/* 折线累计总长 */
static double poly_len(const LPoint *p, int n) {
    double s = 0;
    for (int i = 0; i + 1 < n; ++i) s += dist(p[i], p[i + 1]);
    return s;
}

/* 点在折线上的累计弧长，用于把灯位换算回 s */
static double arc_of(const LPoint *p, int n, LPoint q) {
    double acc = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        double len = sqrt(dx*dx + dy*dy);
        double t = ((q.x - p[i].x) * dx + (q.y - p[i].y) * dy) / (len * len);
        if (t < 0) t = 0; else if (t > 1) t = 1;
        /* 残差判定 q 是否真的在这段上；顶点(t≈0)留给上一段的 t≈1 */
        double rx = q.x - (p[i].x + dx * t);
        double ry = q.y - (p[i].y + dy * t);
        int on = (rx*rx + ry*ry) < 1e-12;
        if (on && (t > 1e-9 || i == 0)) return acc + t * len;
        acc += len;
    }
    return acc;
}

int main(void) {
    /* 直角折线：两段各 100，总长 200 */
    LPoint p[3] = {{0,0},{100,0},{100,100}};
    LLamps out;

    /* --- 错误处理：点距为负（含 NaN），必须报 SPACING 并指出参数 --- */
    CK(lamp_place(p, 3, -5.0, 1, &out) == LAMP_E_SPACING);
    CK(lamp_place(p, 3, -5.0, 0, &out) == LAMP_E_SPACING);
    CK(lamp_place(p, 3, NAN, 1, &out) == LAMP_E_SPACING);
    /* 等距模式下点距为零无法构造，逐像素模式不用 spacing，二者区别对待 */
    CK(lamp_place(p, 3, 0.0, 1, &out) == LAMP_E_SPACING);
    CK(lamp_place(p, 3, 0.0, 0, &out) == LAMP_OK);
    printf("  spacing err: %s\n", lamp_strerror(LAMP_E_SPACING));

    /* --- 错误处理：总长为零（全部重合），必须报 ZERO_LEN 并指出参数 --- */
    LPoint z[3] = {{7,7},{7,7},{7,7}};
    CK(lamp_place(z, 3, 5.0, 1, &out) == LAMP_E_ZERO_LEN);
    CK(lamp_place(z, 3, 5.0, 0, &out) == LAMP_E_ZERO_LEN);
    printf("  zero len err: %s\n", lamp_strerror(LAMP_E_ZERO_LEN));

    /* 通用参数错误 */
    CK(lamp_place(NULL, 3, 5.0, 1, &out) == LAMP_E_ARG);
    CK(lamp_place(p, 1, 5.0, 1, &out) == LAMP_E_ARG);

    /* --- 等距模式：与按总长手算的均匀分布核对 --- */
    double spacing = 10.0;
    CK(lamp_place(p, 3, spacing, 1, &out) == LAMP_OK);
    double total = poly_len(p, 3);
    int expect = (int)ceil(total / spacing) - 1; /* s=d,2d,...,不放终点 */
    CK(out.count == expect);
    printf("  equidistant: total=%.1f spacing=%.1f lamps=%d (expect %d)\n",
           total, spacing, out.count, expect);

    double max_err = 0;
    for (int k = 0; k < out.count; ++k) {
        double s = arc_of(p, 3, out.pts[k]);
        double want = (k + 1) * spacing;          /* 手算均匀位置 */
        double e = fabs(s - want);
        if (e > max_err) max_err = e;
        CK(e < 1e-9);
    }
    printf("  max arc-length error vs hand calc: %.3e\n", max_err);
    CK(max_err < 1e-9);

    /* 相邻灯沿曲线的实际点距应恒等于 spacing（转角处不密不疏）*/
    double prev = 0;
    for (int k = 0; k < out.count; ++k) {
        double s = arc_of(p, 3, out.pts[k]);
        CK(fabs((s - prev) - spacing) < 1e-9);
        prev = s;
    }

    /* spacing 大于总长：零盏灯，不报错 */
    CK(lamp_place(p, 3, 999.0, 1, &out) == LAMP_OK);
    CK(out.count == 0);

    /* --- 逐像素模式：与旧逐像素输出逐位相同（每 0.5 一盏）--- */
    LLamps px;
    CK(lamp_place(p, 3, spacing, 0, &px) == LAMP_OK);
    int expect_px = 0;
    for (int i = 0; i + 1 < 3; ++i)
        expect_px += (int)ceil(dist(p[i], p[i+1]) / 0.5);
    CK(px.count == expect_px);
    /* 逐位重算旧公式 */
    int k = 0;
    for (int i = 0; i + 1 < 3; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        double len = sqrt(dx*dx + dy*dy);
        for (double s = 0.0; s < len; s += 0.5, ++k) {
            CK(fabs(px.pts[k].x - (p[i].x + dx * (s / len))) < 1e-12);
            CK(fabs(px.pts[k].y - (p[i].y + dy * (s / len))) < 1e-12);
        }
    }
    CK(k == px.count);
    printf("  pixel mode: lamps=%d, bitwise matches legacy 0.5 stepping\n", px.count);

    /* --- 导出点距表到 stdout，检查表头与首行 --- */
    CK(lamp_place(p, 3, spacing, 1, &out) == LAMP_OK);
    CK(lamp_export_csv(&out, p, 3, "-") == LAMP_OK);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
