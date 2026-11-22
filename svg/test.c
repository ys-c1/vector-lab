/* svg 模块测试。
 *
 * 重点：
 *   1. 旧接口行为不变：svg_parse 仍拒绝 A；纯 M/L/Z 路径的 svg_fill
 *      与 compat 目标里保留的旧实现逐字节一致。
 *   2. 椭圆弧参数化与逐点代入椭圆方程手算的圆心/起止角一致。
 *   3. 径向偏差 <= 上界，且上界 <= 容差；两档计数都覆盖到。
 *   4. 半径不足 / 起止点重合 / 旋转角越界报错并指出段号。
 *   5. 开关为假时画面与“弧段画整圆”的旧式理解逐字节一致。
 */
#include "svgpath.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)
#define NEAR(a, b, e) (fabs((double)(a) - (double)(b)) <= (e))

#define PI 3.14159265358979323846

static VSeg mk_arc(double rx, double ry, double deg, int large, int sweep,
                   double x1, double y1, double x2, double y2) {
    VSeg s;
    memset(&s, 0, sizeof s);
    s.kind = SV_ARC;
    s.a = (VPoint){x1, y1};
    s.b = (VPoint){x2, y2};
    s.rx = rx;
    s.ry = ry;
    s.xrot = deg * PI / 180.0;
    s.large = large;
    s.sweep = sweep;
    return s;
}

/* 逐点代入椭圆方程手算：对弧上参数角 t，返回该点的“到圆心距离减去
 * 该方向的椭圆半径”。这正是径向偏差的定义，与库内折线无关。 */
static double hand_dev(const SvgArcFrame *f, double x, double y) {
    double dx = x - f->center.x, dy = y - f->center.y;
    double c = cos(f->xrot), sn = sin(f->xrot);
    double u =  c * dx + sn * dy;
    double v = -sn * dx + c * dy;
    double ang = atan2(v, u);
    double rr = 1.0 / sqrt(cos(ang) * cos(ang) / (f->rx * f->rx)
                         + sin(ang) * sin(ang) / (f->ry * f->ry));
    return fabs(sqrt(u * u + v * v) - rr);
}

static int count_on(const unsigned char *b, int n) {
    int on = 0;
    for (int i = 0; i < n; ++i) if (b[i]) ++on;
    return on;
}

/* 1. 旧解析接口对 A 仍然报错；打开开关后才解析。 */
static void test_parse_flags(void) {
    VPath p;
    CK(svg_parse("M0,0 A5,5 0 0 1 10,0", &p) == SVG_E_CMD);
    CK(svg_last_error_detail()->seg == 0);
    CK(svg_parse_flags("M0,0 A5,5 0 0 1 10,0", &p, 0) == SVG_E_CMD);
    CK(svg_parse_flags("M0,0 A5,5 0 0 1 10,0", &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 1);
    CK(p.segs[0].kind == SV_ARC);
    CK(p.segs[0].large == 0 && p.segs[0].sweep == 1);
    CK(p.segs[0].rx == 5.0 && p.segs[0].ry == 5.0 && p.segs[0].xrot == 0.0);
    CK(p.segs[0].a.x == 0.0 && p.segs[0].a.y == 0.0);
    CK(p.segs[0].b.x == 10.0 && p.segs[0].b.y == 0.0);

    /* 相对命令 a：x/y 相对当前点，rx/ry 不相对 */
    CK(svg_parse_flags("M3,4 a2,1 30 1 0 6,8", &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 1 && p.segs[0].kind == SV_ARC);
    CK(p.segs[0].b.x == 9.0 && p.segs[0].b.y == 12.0);
    CK(NEAR(p.segs[0].xrot, 30.0 * PI / 180.0, 1e-15));
    CK(p.segs[0].large == 1 && p.segs[0].sweep == 0);

    /* 标志位与数字紧贴也能拆开："0110 0" */
    CK(svg_parse_flags("M0,0 A5,5 0 0110,0", &p, SVG_F_ARCS) == SVG_OK);
    CK(p.segs[0].large == 0 && p.segs[0].sweep == 1);
    CK(p.segs[0].b.x == 10.0 && p.segs[0].b.y == 0.0);

    /* 多个弧段连写，段号按序递增 */
    CK(svg_parse_flags("M0,0 A2,2 0 0 1 4,0 A3,1 45 1 0 9,4", &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 2);
    CK(p.segs[1].kind == SV_ARC && p.segs[1].rx == 3.0 && p.segs[1].ry == 1.0);
    CK(p.segs[1].a.x == 4.0 && p.segs[1].b.x == 9.0 && p.segs[1].b.y == 4.0);

    /* 未知标志位 */
    CK(svg_parse_flags("M0,0 L1,1", &p, 8u) == SVG_E_FLAGS);
    CK(svg_last_error_detail()->code == SVG_E_FLAGS);

    /* 解析期只查数字与有限性：非有限值直接拒，并指出段号与字段 */
    CK(svg_parse_flags("M0,0 A5,5 0 0 1 nan,0", &p, SVG_F_ARCS) == SVG_E_NONFINITE);
    CK(svg_last_error_detail()->seg == 0);
    CK(strcmp(svg_last_error_detail()->field, "x") == 0);
    CK(svg_parse_flags("M0,0 L1,1 A5,5 0 0 1 inf,0", &p, SVG_F_ARCS) == SVG_E_NONFINITE);
    CK(svg_last_error_detail()->seg == 1);
    CK(strcmp(svg_last_error_detail()->field, "x") == 0);
    CK(svg_parse_flags("M0,0 A5,5 0 0 1 10,0 L0,0 A5,5 0 0 1 10,inf", &p, SVG_F_ARCS) == SVG_E_NONFINITE);
    CK(svg_last_error_detail()->seg == 2);
    CK(strcmp(svg_last_error_detail()->field, "y") == 0);

    /* 旧测试的错误码不变 */
    CK(svg_parse("L0,0", &p) == SVG_E_START);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &p) == SVG_E_CMD);
    CK(svg_parse("M0,0 L", &p) == SVG_E_NUM);
    CK(svg_parse(NULL, &p) == SVG_E_ARG);
}

/* 2. 圆心与起止角：对照独立手算的数值（Python 按 F.6.5 逐式算得）。 */
static void test_arc_frame(void) {
    struct Exp {
        double rx, ry, deg;
        int large, sweep;
        double x1, y1, x2, y2;
        double cx, cy, t1, dt;   /* 度 */
    } ex[] = {
        {5, 5, 0, 0, 1, 0, 0, 10, 0, 5.0, 0.0, 180.0, 180.0},
        {5, 5, 0, 0, 0, 0, 0, 10, 0, 5.0, 0.0, 180.0, -180.0},
        {10, 5, 30, 0, 0, 0, 0, 10, 0,
         1.317573102, -4.960783708, 82.303016758, -82.819244219},
        {10, 5, 30, 0, 1, 0, 0, 10, 0,
         8.682426898, 4.960783708, 179.483772540, 82.819244219},
        {10, 5, 30, 1, 0, 0, 0, 10, 0,
         8.682426898, 4.960783708, 179.483772540, -277.180755781},
        {10, 5, 30, 1, 1, 0, 0, 10, 0,
         1.317573102, -4.960783708, 82.303016758, 277.180755781},
        {10, 5, 90, 0, 1, 0, 0, 10, 0, 5.0, 0.0, 90.0, 180.0},
    };
    for (unsigned i = 0; i < sizeof ex / sizeof ex[0]; ++i) {
        VSeg s = mk_arc(ex[i].rx, ex[i].ry, ex[i].deg, ex[i].large,
                        ex[i].sweep, ex[i].x1, ex[i].y1, ex[i].x2, ex[i].y2);
        SvgArcFrame f;
        CK(svg_arc_frame(0, &s, &f) == SVG_OK);
        CK(NEAR(f.center.x, ex[i].cx, 1e-8));
        CK(NEAR(f.center.y, ex[i].cy, 1e-8));
        CK(NEAR(f.theta1 * 180.0 / PI, ex[i].t1, 1e-7));
        CK(NEAR(f.dtheta * 180.0 / PI, ex[i].dt, 1e-7));
        CK(NEAR(f.theta2, f.theta1 + f.dtheta, 0.0));
        CK(f.scale == 1.0);

        /* 弧端点必须精确落在起止点上（手算核对：椭圆参数式代回） */
        double c = cos(f.xrot), sn = sin(f.xrot);
        double u = f.rx * cos(f.theta1), v = f.ry * sin(f.theta1);
        double px = f.center.x + c * u - sn * v;
        double py = f.center.y + sn * u + c * v;
        CK(NEAR(px, ex[i].x1, 1e-9) && NEAR(py, ex[i].y1, 1e-9));
        u = f.rx * cos(f.theta2); v = f.ry * sin(f.theta2);
        px = f.center.x + c * u - sn * v;
        py = f.center.y + sn * u + c * v;
        CK(NEAR(px, ex[i].x2, 1e-9) && NEAR(py, ex[i].y2, 1e-9));

        /* 大弧标志：|dtheta| >= π，小弧 <= π */
        CK(ex[i].large ? fabs(f.dtheta) >= PI - 1e-9
                       : fabs(f.dtheta) <= PI + 1e-9);
        /* 扫描方向：sweep=1 -> dtheta>0 */
        CK(ex[i].sweep ? f.dtheta > 0.0 : f.dtheta < 0.0);

        /* 逐点代入椭圆方程：弧中点满足 (u/rx)^2 + (v/ry)^2 = 1 */
        double tm = f.theta1 + f.dtheta * 0.5;
        u = f.rx * cos(tm); v = f.ry * sin(tm);
        px = f.center.x + c * u - sn * v;
        py = f.center.y + sn * u + c * v;
        double qx = (c * (px - f.center.x) + sn * (py - f.center.y)) / f.rx;
        double qy = (-sn * (px - f.center.x) + c * (py - f.center.y)) / f.ry;
        CK(NEAR(qx * qx + qy * qy, 1.0, 1e-12));
    }
}

/* 3. 径向偏差 <= 上界，上界 <= 容差；两档计数都对，且 nminus <= nplus。 */
static void test_chords_and_deviation(void) {
    struct Case { double rx, ry, deg; int large, sweep;
                  double x1, y1, x2, y2; } cs[] = {
        {5, 5, 0, 0, 1, 0, 0, 10, 0},
        {10, 5, 30, 0, 0, 0, 0, 10, 0},
        {10, 5, 30, 1, 1, 0, 0, 10, 0},
        {20, 10, 0, 0, 1, 5, 25, 45, 25},
        {8, 80, 15, 0, 1, 1, 1, 11, 11},
        {40, 5, -60, 1, 0, 0, 0, 10, 0},
        {0.5, 0.25, 0, 0, 1, 0, 0, 0.8, 0},
    };
    double tols[] = {1e-1, 1e-2, 1e-3};
    for (unsigned i = 0; i < sizeof cs / sizeof cs[0]; ++i) {
        VSeg s = mk_arc(cs[i].rx, cs[i].ry, cs[i].deg, cs[i].large,
                        cs[i].sweep, cs[i].x1, cs[i].y1, cs[i].x2, cs[i].y2);
        SvgArcFrame f;
        CK(svg_arc_frame(0, &s, &f) == SVG_OK);
        for (unsigned k = 0; k < sizeof tols / sizeof tols[0]; ++k) {
            double tol = tols[k];
            int n, np, nm;
            double bound;
            CK(svg_arc_chords(0, &s, tol, &n, &bound, &np, &nm) == SVG_OK);
            CK(n == np && n >= 1 && nm >= 1 && nm <= np);
            CK(bound <= tol * (1 + 1e-12));

            /* 逐段检查实测偏差（在弦上取 33 个内点，用椭圆方程手算） */
            VPoint pts[9000];
            int got;
            double bg;
            CK(svg_arc_polyline(0, &s, tol, pts, 9000, &got, &bg) == SVG_OK);
            CK(got == n && bg == bound);
            CK(NEAR(pts[0].x, s.a.x, 0.0) && NEAR(pts[0].y, s.a.y, 0.0));
            CK(NEAR(pts[n].x, s.b.x, 0.0) && NEAR(pts[n].y, s.b.y, 0.0));

            double worst = 0.0;
            for (int e = 0; e < n; ++e) {
                for (int j = 1; j < 32; ++j) {
                    double t = (double)j / 32.0;
                    double x = pts[e].x + (pts[e + 1].x - pts[e].x) * t;
                    double y = pts[e].y + (pts[e + 1].y - pts[e].y) * t;
                    double d = hand_dev(&f, x, y);
                    if (d > worst) worst = d;
                }
            }
            CK(worst <= bound * (1 + 1e-9) + 1e-15);
            CK(worst <= tol * (1 + 1e-9) + 1e-15);
        }
    }
}

/* 4. 错误路径：半径不足 / 起止点重合 / 旋转角越界，段号与字段名正确。 */
static void test_errors(void) {
    VPath p;
    SvgArcFrame f;

    /* 半径不足：rx=ry=1 连 (0,0)-(10,0) 需要放大 5 倍；解析通过，填充报错 */
    CK(svg_parse_flags("M0,0 L2,2 A1,1 0 0 1 12,2", &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 2 && p.segs[1].kind == SV_ARC);
    CK(svg_arc_frame(1, &p.segs[1], &f) == SVG_E_RADIUS);
    CK(svg_last_error_detail()->seg == 1);
    CK(strcmp(svg_last_error_detail()->field, "rx/ry") == 0);
    CK(NEAR(svg_last_error_detail()->value, 5.0, 1e-12));
    CK(NEAR(f.scale, 5.0, 1e-12) && NEAR(f.rx, 5.0, 1e-12));

    unsigned char buf[32 * 32];
    CK(svg_fill_ex(&p, 32, 32, buf, SVG_ARC_ELLIPSE, 1e-3, NULL) == SVG_E_RADIUS);
    CK(svg_last_error_detail()->seg == 1);
    CK(strcmp(svg_last_error_detail()->field, "rx/ry") == 0);

    /* 起止点重合：解析通过（写入零长弧），参数化时报错 */
    CK(svg_parse_flags("M3,3 A5,2 10 1 1 3,3", &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 1);
    CK(svg_arc_frame(0, &p.segs[0], &f) == SVG_E_COINCIDENT);
    CK(svg_last_error_detail()->seg == 0);
    CK(strcmp(svg_last_error_detail()->field, "x/y") == 0);

    /* 旋转角越界：45° 合法，91° / -100° 报错 */
    CK(svg_parse_flags("M0,0 A5,10 90 0 1 10,0", &p, SVG_F_ARCS) == SVG_OK);
    CK(svg_arc_frame(0, &p.segs[0], &f) == SVG_OK);
    CK(svg_parse_flags("M0,0 A5,2 91 0 1 10,0", &p, SVG_F_ARCS) == SVG_OK);
    CK(svg_arc_frame(0, &p.segs[0], &f) == SVG_E_XROT);
    CK(svg_last_error_detail()->seg == 0);
    CK(strcmp(svg_last_error_detail()->field, "xrot") == 0);
    CK(NEAR(svg_last_error_detail()->value, 91.0, 1e-12));
    CK(svg_parse_flags("M0,0 L1,1 A5,2 -100 0 1 10,0", &p, SVG_F_ARCS) == SVG_OK);
    CK(svg_arc_frame(1, &p.segs[1], &f) == SVG_E_XROT);
    /* 不做 360° 折回：450° 同样按越界处理 */
    CK(svg_parse_flags("M0,0 L1,1 A5,2 450 0 1 10,0", &p, SVG_F_ARCS) == SVG_OK);
    CK(svg_arc_frame(1, &p.segs[1], &f) == SVG_E_XROT);
    CK(svg_last_error_detail()->seg == 1);

    /* 负半径按 F.6.2 取绝对值，不报错 */
    VSeg neg = mk_arc(-5, -2, 0, 0, 1, 0, 0, 10, 0);
    CK(svg_arc_frame(0, &neg, &f) == SVG_OK);
    CK(f.rx == 5.0 && f.ry == 2.0);

    /* rx==0 报错字段名 rx */
    VSeg z = mk_arc(0, 2, 0, 0, 1, 0, 0, 10, 0);
    CK(svg_arc_frame(0, &z, &f) == SVG_E_RADIUS);
    CK(strcmp(svg_last_error_detail()->field, "rx") == 0);

    /* 非弧段传给参数化 */
    VSeg line;
    memset(&line, 0, sizeof line);
    line.kind = SV_LINE;
    CK(svg_arc_frame(0, &line, &f) == SVG_E_ARG);
}

/* 5. 开关为假：svg_fill 与“弧段只画整圆”的显式写法逐字节一致；
 *    开关为真：椭圆弧采样不同、像素数不同。 */
static void test_fill_modes(void) {
    const char *d = "M2,20 A20,10 30 0 1 30,20 L30,30 Z";
    VPath p;
    CK(svg_parse_flags(d, &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 3);   /* 弧 + L + Z 闭合直线 */

    int w = 48, h = 48, n = w * h;
    unsigned char *a = malloc((size_t)n), *b = malloc((size_t)n);
    unsigned char *e = malloc((size_t)n), *em = malloc((size_t)n);
    SvgFillStats sa, sb, se, sem;

    CK(svg_fill_ex(&p, w, h, a, SVG_ARC_CIRCLE, 1e-3, &sa) == SVG_OK);
    CK(svg_fill(&p, w, h, a) == SVG_OK);
    CK(svg_fill_ex(&p, w, h, b, SVG_ARC_CIRCLE, 1e-3, &sb) == SVG_OK);
    CK(memcmp(a, b, (size_t)n) == 0);
    CK(sa.whole == 1 && sb.whole == 1);
    CK(sa.arcs == 1 && sb.arcs == 1);
    CK(sa.chords == sb.chords);

    /* 兼容整圆：逐弧记录里 whole=1，圆心就是弧起点，半径就是 rx */
    CK(svg_arc_log_count() == 1);
    const SvgArcRow *row = svg_arc_log();
    CK(row[0].whole == 1);
    CK(row[0].seg == 0);
    CK(NEAR(row[0].center.x, 2.0, 0.0) && NEAR(row[0].center.y, 20.0, 0.0));
    CK(NEAR(row[0].rx, 20.0, 0.0) && NEAR(row[0].ry, 20.0, 0.0));
    CK(NEAR(row[0].dtheta, 2.0 * PI, 0.0));

    CK(svg_fill_ex(&p, w, h, e, SVG_ARC_ELLIPSE, 1e-3, &se) == SVG_OK);
    CK(svg_fill_ex(&p, w, h, em, SVG_ARC_ELLIPSE_MIN, 1e-3, &sem) == SVG_OK);
    CK(se.whole == 0 && sem.whole == 0);
    CK(memcmp(a, e, (size_t)n) != 0);
    CK(svg_arc_log_count() == 1);
    row = svg_arc_log();
    CK(row[0].whole == 0 && NEAR(row[0].rx, 20.0, 0.0) && NEAR(row[0].ry, 10.0, 0.0));
    CK(row[0].nplus >= row[0].nminus);
    CK(row[0].n == row[0].nminus);           /* 最近一次是紧凑档 */
    CK(se.nplus >= se.nminus && sem.nplus >= sem.nminus);
    CK(se.chords == (long)(se.nplus + 2));   /* 余量档：弧弦 + L + Z 两条直线 */
    CK(sem.chords == (long)(sem.nminus + 2));

    int on_circle = count_on(a, n), on_ell = count_on(e, n), on_min = count_on(em, n);
    CK(on_circle > 0 && on_ell > 0 && on_min > 0);
    CK(on_circle != on_ell || on_min != on_ell);

    /* 纯线段路径：三种模式输出一致（弧模式不影响非弧段） */
    VPath q;
    CK(svg_parse("M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z", &q) == SVG_OK);
    unsigned char *qa = malloc((size_t)n), *qb = malloc((size_t)n);
    CK(svg_fill(&q, w, h, qa) == SVG_OK);
    CK(svg_fill_ex(&q, w, h, qb, SVG_ARC_ELLIPSE, 1e-3, NULL) == SVG_OK);
    CK(memcmp(qa, qb, (size_t)n) == 0);
    free(qa); free(qb);

    /* 非法 arc_mode / 非法 tol 的处理 */
    CK(svg_fill_ex(&p, w, h, b, 7u, 1e-3, NULL) == SVG_E_FLAGS);
    CK(svg_fill_ex(&p, w, h, b, SVG_ARC_ELLIPSE, -1.0, &sb) == SVG_OK);
    CK(sb.tol == SVG_DEFAULT_TOL);
    CK(svg_fill_ex(&p, w, h, b, SVG_ARC_ELLIPSE, NAN, &sb) == SVG_OK);
    CK(sb.tol == SVG_DEFAULT_TOL);

    /* 整圆兼容对合法椭圆弧没有几何要求：负 xrot、起止点重合都能画 */
    VPath r;
    CK(svg_parse_flags("M10,10 A4,1 0 1 0 10,10", &r, SVG_F_ARCS) == SVG_OK);
    CK(svg_fill(&r, w, h, b) == SVG_OK);
    CK(svg_fill_ex(&r, w, h, b, SVG_ARC_ELLIPSE, 1e-3, NULL) == SVG_E_COINCIDENT);

    free(a); free(b); free(e); free(em);
}

/* 6. 椭圆弧填充像素数与几何相符：实心椭圆面积应接近 π*rx*ry。 */
static void test_ellipse_area(void) {
    VPath p;
    /* 以 (50,40) 为中心、rx=30 ry=15 的整椭圆：两段半弧拼成。
     * 圆心恰好落在整像素坐标，面积可直接与 πab 比。 */
    CK(svg_parse_flags("M20,40 A30,15 0 0 1 80,40 A30,15 0 0 1 20,40 Z",
                       &p, SVG_F_ARCS) == SVG_OK);
    CK(p.count == 3);
    int w = 120, h = 80;
    unsigned char *buf = malloc((size_t)w * h);
    CK(svg_fill_ex(&p, w, h, buf, SVG_ARC_ELLIPSE, 1e-4, NULL) == SVG_OK);
    int on = count_on(buf, w * h);
    double want = PI * 30.0 * 15.0;
    CK(fabs((double)on - want) / want < 0.02);
    /* 中心点被填、外点不填 */
    CK(buf[40 * w + 60] != 0);
    CK(buf[0] == 0);
    free(buf);
}

int main(void) {
    test_parse_flags();
    test_arc_frame();
    test_chords_and_deviation();
    test_errors();
    test_fill_modes();
    test_ellipse_area();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
