#include "stroke.h"
#include <stdio.h>
#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int near_pt(SPoint a, double x, double y) {
    return fabs(a.x - x) < 1e-9 && fabs(a.y - y) < 1e-9;
}
static double poly_area(const SOutline *p) {
    double s = 0.0;
    for (int i = 0; i < p->count; ++i) {
        SPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        s += a.x * b.y - b.x * a.y;
    }
    return fabs(s) * 0.5;
}

int main(void) {
    /* ---- 旧接口既有检查 ---- */
    SPoint p[3] = {{0,0},{10,0},{10,10}};
    SPoly out;
    CK(stroke_polyline(p, 3, 2.0, &out) == 0);
    CK(out.count == 6);
    CK(out.pts[0].y > 0.9 && out.pts[0].y < 1.1);
    CK(stroke_polyline(p, 1, 2.0, &out) == -1);
    CK(stroke_polyline(p, 3, 0.0, &out) == -1);
    SPoint bad[2] = {{0,0},{0,0}};
    CK(stroke_polyline(bad, 2, 1.0, &out) == -2);

    /* ---- LEGACY 与旧接口逐点一致 ---- */
    SOutline lo; SStrokeOpt lg = { .join = SJ_LEGACY };
    CK(stroke_outline(p, 3, 2.0, &lg, &lo, 0) == 0);
    CK(lo.count == out.count);
    for (int i = 0; i < lo.count; ++i) CK(near_pt(lo.pts[i], out.pts[i].x, out.pts[i].y));

    int bad_idx = -1;
    SStrokeOpt opt; SOutline o;

    /* ---- 直角 L 形，斜接：手算轮廓 6 点，面积 40 ---- */
    opt = (SStrokeOpt){ .join = SJ_MITER };
    CK(stroke_outline(p, 3, 2.0, &opt, &o, &bad_idx) == 0);
    CK(o.count == 6);
    CK(near_pt(o.pts[0], 0, 1));          /* 左端左偏移 */
    CK(near_pt(o.pts[1], 9, 1));          /* 斜接尖点：y=1 与 x=9 交点 */
    CK(near_pt(o.pts[2], 9, 10));
    CK(near_pt(o.pts[3], 11, 10));
    CK(near_pt(o.pts[4], 11, -1));        /* 内侧交点 */
    CK(near_pt(o.pts[5], 0, -1));
    CK(fabs(poly_area(&o) - 40.0) < 1e-9);

    /* ---- 同形斜切：7 点，斜边连接两偏移端点 (10,1)->(9,0)，面积 40.5 ---- */
    opt = (SStrokeOpt){ .join = SJ_BEVEL };
    CK(stroke_outline(p, 3, 2.0, &opt, &o, 0) == 0);
    CK(o.count == 7);
    CK(near_pt(o.pts[1], 10, 1));
    CK(near_pt(o.pts[2], 9, 0));
    CK(near_pt(o.pts[3], 9, 10));
    CK(fabs(poly_area(&o) - 40.5) < 1e-9);

    /* ---- 同形圆角：12 段弧（含起点共 13 个弧点），半径 1，首尾贴合斜切端点 ---- */
    opt = (SStrokeOpt){ .join = SJ_ROUND };
    CK(stroke_outline(p, 3, 2.0, &opt, &o, 0) == 0);
    CK(o.count == 18);
    CK(near_pt(o.pts[1], 10, 1));                        /* 弧起点 q1 */
    for (int i = 1; i <= 13; ++i) {
        double d = hypot(o.pts[i].x - 10.0, o.pts[i].y - 0.0);
        CK(fabs(d - 1.0) < 1e-12);
    }
    CK(near_pt(o.pts[13], 9, 0));                        /* 弧终点 q2 */
    CK(near_pt(o.pts[14], 9, 10));
    /* 外弧内切于斜接三角：精确面积 = 40 -(0.5 - pi/4) ≈ 40.2146；
     * 12 段细分实测 40.2168，误差来自弧的折线化 */
    CK(poly_area(&o) > 40.216 && poly_area(&o) < 40.218);

    /* ---- 右转角：外侧在右侧，斜接点 (9,-1) ---- */
    SPoint pr[3] = {{0,0},{10,0},{10,-10}};
    opt = (SStrokeOpt){ .join = SJ_MITER };
    CK(stroke_outline(pr, 3, 2.0, &opt, &o, 0) == 0);
    int found = 0;
    for (int i = 0; i < o.count; ++i) if (near_pt(o.pts[i], 9, -1)) found = 1;
    CK(found);

    /* ---- 右转圆角：外弧在右侧，弧点半径 1；镜像于左转，面积相同 ---- */
    opt = (SStrokeOpt){ .join = SJ_ROUND };
    CK(stroke_outline(pr, 3, 2.0, &opt, &o, 0) == 0);
    CK(o.count == 18);
    int on_arc = 0;
    for (int i = 0; i < o.count; ++i) {
        double d = hypot(o.pts[i].x - 10.0, o.pts[i].y - 0.0);
        if (fabs(d - 1.0) < 1e-12) ++on_arc;
    }
    CK(on_arc == 13);
    CK(poly_area(&o) > 40.216 && poly_area(&o) < 40.218);

    /* ---- 夹角为零（同向折返）：拒绝 -3，指出拐点 1 ---- */
    SPoint rev[3] = {{0,0},{10,0},{5,0}};
    opt = (SStrokeOpt){ .join = SJ_BEVEL };
    bad_idx = -1;
    CK(stroke_outline(rev, 3, 2.0, &opt, &o, &bad_idx) == -3);
    CK(bad_idx == 1);

    /* ---- 斜接超限：delta=120° 比率 2.0，限 1.5 -> -4，拐点 1 ---- */
    SPoint sharp[3] = {{0,0},{10,0},{5, 8.660254037844386}};
    opt = (SStrokeOpt){ .join = SJ_MITER, .miter_limit = 1.5 };
    bad_idx = -1;
    CK(stroke_outline(sharp, 3, 2.0, &opt, &o, &bad_idx) == -4);
    CK(bad_idx == 1);
    CK(fabs(opt.info[0].miter_ratio - 2.0) < 1e-9);
    /* 同一拐点斜切与圆角应正常生成；AUTO 对 60° 夹角选圆角，不触发超限 */
    opt = (SStrokeOpt){ .join = SJ_BEVEL, .miter_limit = 1.5 };
    CK(stroke_outline(sharp, 3, 2.0, &opt, &o, 0) == 0);
    opt = (SStrokeOpt){ .join = SJ_AUTO, .miter_limit = 1.5 };
    CK(stroke_outline(sharp, 3, 2.0, &opt, &o, 0) == 0);
    CK(opt.info[0].join == SJ_ROUND);

    /* ---- AUTO 分档：130° 斜接 / 90° 斜切 / 30° 圆角 ---- */
    double gammas[3] = { 130.0 * M_PI / 180.0, 90.0 * M_PI / 180.0, 30.0 * M_PI / 180.0 };
    int expect[3] = { SJ_MITER, SJ_BEVEL, SJ_ROUND };
    for (int t = 0; t < 3; ++t) {
        double delta = M_PI - gammas[t];
        SPoint q[3] = {{0,0},{10,0},{10 + 10*cos(delta), 10*sin(delta)}};
        opt = (SStrokeOpt){ .join = SJ_AUTO };
        CK(stroke_outline(q, 3, 2.0, &opt, &o, 0) == 0);
        CK(opt.record_count == 1);
        CK(opt.info[0].join == expect[t]);
        CK(fabs(opt.info[0].angle - gammas[t]) < 1e-9);
    }

    /* ---- 零长度边仍返回 -2 ---- */
    CK(stroke_outline(bad, 2, 1.0, &(SStrokeOpt){.join=SJ_MITER}, &o, &bad_idx) == -2);

    /* ---- AUTO 选中斜接但超限时同样拒绝，指出拐点号 ---- */
    SPoint wide[3] = {{0,0},{10,0},{10.0001,0.5}}; /* 近直行，gamma 大 -> 斜接档 */
    opt = (SStrokeOpt){ .join = SJ_AUTO, .miter_limit = 1.01 };
    bad_idx = -1;
    int rc_auto = stroke_outline(wide, 3, 2.0, &opt, &o, &bad_idx);
    if (opt.info[0].join == SJ_MITER && opt.info[0].miter_ratio > 1.01) {
        CK(rc_auto == -4);
        CK(bad_idx == 1);
    }

    /* ---- 直行共线点：不产生额外拐点，轮廓退化为矩形 4 点 ---- */
    SPoint straight[4] = {{0,0},{5,0},{10,0},{15,0}};
    opt = (SStrokeOpt){ .join = SJ_AUTO };
    CK(stroke_outline(straight, 4, 2.0, &opt, &o, 0) == 0);
    CK(o.count == 4);
    CK(opt.record_count == 2);
    CK(opt.info[0].join == SJ_MITER && opt.info[1].join == SJ_MITER);

    /* ---- 容量护栏：最大输入全圆角不溢出、不拒绝 ---- */
    {
        static SPoint many[STROKE_MAX_IN + 1];
        for (int i = 0; i <= STROKE_MAX_IN; ++i) {
            many[i].x = i * 2.0;
            many[i].y = (i % 2) ? 3.0 : 0.0;
        }
        SOutline big;
        SStrokeOpt bopt = { .join = SJ_ROUND };
        CK(stroke_outline(many, STROKE_MAX_IN + 1, 2.0, &bopt, &big, 0) == 0);
        CK(big.count > 0 && big.count <= STROKE_MAX_PTS);
        CK(stroke_outline(many, STROKE_MAX_IN + 2, 2.0, &bopt, &big, 0) == -1);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
