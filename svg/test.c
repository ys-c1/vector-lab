#include "svgpath.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

#define W 50
#define H 50

/* 外框 40×40 + 内框 20×20，两框都逆时针；总面积/洞面积都便于手算。 */
static const char *RING_CCW =
    "M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z";
/* 内框改成顺时针：非零规则下成为洞。 */
static const char *RING_CW =
    "M5,5 L45,5 L45,45 L5,45 Z M15,15 L15,35 L35,35 L35,15 Z";

static int count_on(const unsigned char *buf, int n) {
    int on = 0;
    for (int i = 0; i < n; ++i) if (buf[i]) ++on;
    return on;
}

/* 旧接口与退化子路径视图的逐字节对照。 */
static void check_legacy_identical(const char *d) {
    VPath old;
    VSubPaths subs;
    int r_old = svg_parse(d, &old);
    int r_new = svg_parse_legacy(d, &subs);
    if (r_old != 0) { CK(r_new != 0); return; }
    CK(r_new == 0);
    unsigned char *a = (unsigned char *)malloc(W * H);
    unsigned char *b = (unsigned char *)malloc(W * H);
    CK(svg_fill(&old, W, H, a) == 0);
    SVGStats st;
    CK(svg_fill_rule(&subs, W, H, b, SVG_FILL_LEGACY, &st) == 0);
    CK(old.count == subs.path.count);
    /* 结构体尾部有未写的填充字节，逐段比较。 */
    int same = 1;
    for (int i = 0; i < old.count; ++i) {
        const VSeg *u = &old.segs[i], *v = &subs.path.segs[i];
        if (u->kind != v->kind || u->a.x != v->a.x || u->a.y != v->a.y ||
            u->b.x != v->b.x || u->b.y != v->b.y) same = 0;
    }
    CK(same);
    CK(memcmp(a, b, (size_t)W * H) == 0);
    free(a); free(b);
}

int main(void) {
    VSubPaths p;
    SVGStats st;
    unsigned char *buf = (unsigned char *)malloc(W * H);

    /* --- 解析：子路径切分 --- */
    CK(svg_parse_ex("M0,0 L10,0 L10,10 Z", &p) == 0);
    CK(p.sub_count == 1 && p.subs[0].count == 3 && p.subs[0].closed);
    CK(p.path.count == 3);

    CK(svg_parse_ex(RING_CCW, &p) == 0);
    CK(p.sub_count == 2);
    CK(p.subs[0].first == 0 && p.subs[0].count == 4);
    CK(p.subs[1].first == 4 && p.subs[1].count == 4);
    CK(p.path.count == 8);

    /* 相对命令与重复 Z 不重复加闭合边 */
    CK(svg_parse_ex("m5,5 l10,0 l0,10 z Z", &p) == 0);
    CK(p.sub_count == 1 && p.subs[0].count == 3 && p.path.count == 3);

    /* --- 错误：空路径 --- */
    CK(svg_parse_ex("", &p) == SVG_E_EMPTY);
    CK(svg_parse_ex("   ,, ", &p) == SVG_E_EMPTY);
    VSubPaths empty;
    memset(&empty, 0, sizeof empty);
    CK(svg_fill_rule(&empty, W, H, buf, SVG_FILL_EVENODD, &st) == SVG_E_EMPTY);
    CK(st.bad_sub == 0);

    /* --- 错误：未闭合指出子路径号 --- */
    CK(svg_parse_ex("M0,0 L10,0 L10,10", &p) == SVG_E_UNCLOSED);
    CK(p.bad_sub == 1);
    CK(svg_parse_ex("M0,0 L10,0 L10,10 Z M20,20 L30,20 L30,30", &p)
       == SVG_E_UNCLOSED);
    CK(p.bad_sub == 2);
    CK(svg_parse_ex("M0,0 L10,0 L10,10 M20,20 L30,20 L30,30 Z", &p)
       == SVG_E_UNCLOSED);
    CK(p.bad_sub == 1);

    /* 未闭合视图直接交给填充也要报子路径号 */
    CK(svg_parse_ex("M0,0 L10,0 L10,10 Z M20,20 L30,20 L30,30", &p)
       == SVG_E_UNCLOSED);
    CK(svg_fill_rule(&p, W, H, buf, SVG_FILL_EVENODD, &st) == SVG_E_UNCLOSED);
    CK(st.bad_sub == 2);
    double ar = -1.0;
    CK(svg_fill_area(&p, SVG_FILL_EVENODD, &ar, &st) == SVG_E_UNCLOSED);
    CK(st.bad_sub == 2);

    CK(svg_fill_rule(&empty, W, H, buf, 99, &st) == SVG_E_RULE);
    CK(svg_parse_ex("M0,0 L10,0 L10,10 Z", &p) == 0);
    CK(svg_fill_rule(&p, W, H, buf, 99, &st) == SVG_E_RULE);

    /* --- 双环：奇偶规则挖洞 --- */
    CK(svg_parse_ex(RING_CCW, &p) == 0);
    CK(svg_fill_rule(&p, W, H, buf, SVG_FILL_EVENODD, &st) == 0);
    CK(st.subpaths == 2 && st.holes == 1 && st.hole_sub == 2);
    CK(fabs(st.hole_area - 400.0) < 1e-9);        /* 20×20 */
    CK(st.sub_is_hole[0] == 0 && st.sub_is_hole[1] == 1);
    CK(fabs(st.sub_area[0] - 1600.0) < 1e-9);
    CK(count_on(buf, W * H) == 1200);             /* 1600 - 400 */

    /* 非零 + 同向：内环仍落墨，没有洞 */
    CK(svg_fill_rule(&p, W, H, buf, SVG_FILL_NONZERO, &st) == 0);
    CK(st.holes == 0 && st.hole_sub == 0 && st.hole_area == 0.0);
    CK(count_on(buf, W * H) == 1600);

    /* 非零 + 内环反向：洞成立 */
    CK(svg_parse_ex(RING_CW, &p) == 0);
    CK(svg_fill_rule(&p, W, H, buf, SVG_FILL_NONZERO, &st) == 0);
    CK(st.holes == 1 && st.hole_sub == 2);
    CK(fabs(st.hole_area - 400.0) < 1e-9);
    CK(count_on(buf, W * H) == 1200);

    /* --- 解析法面积与手算、与像素计数一致 --- */
    CK(svg_parse_ex(RING_CCW, &p) == 0);
    double area_ccw = 0.0, area_cw = 0.0;
    CK(svg_fill_area(&p, SVG_FILL_EVENODD, &area_ccw, &st) == 0);
    CK(fabs(area_ccw - (1600.0 - 400.0)) < 1e-9);
    CK(fabs(area_ccw - 1200.0) < 1e-9);
    CK(svg_fill_area(&p, SVG_FILL_NONZERO, &area_ccw, &st) == 0);
    CK(fabs(area_ccw - 1600.0) < 1e-9);
    CK(svg_parse_ex(RING_CW, &p) == 0);
    CK(svg_fill_area(&p, SVG_FILL_NONZERO, &area_cw, &st) == 0);
    CK(fabs(area_cw - 1200.0) < 1e-9);


    /* 自交蝴蝶形：奇偶规则面积 = 两个三角形，非零规则按绕向合并 */
    CK(svg_parse_ex("M0,0 L10,10 L10,0 L0,10 Z", &p) == 0);
    double ab = 0.0, an = 0.0;
    CK(svg_fill_area(&p, SVG_FILL_EVENODD, &ab, &st) == 0);
    CK(svg_fill_area(&p, SVG_FILL_NONZERO, &an, &st) == 0);
    CK(fabs(ab - 50.0) < 1e-9);    /* 2 × 5×5 三角形 */
    CK(fabs(an - 50.0) < 1e-9);

    /* --- 退化路径逐字节一致 --- */
    check_legacy_identical("M0,0 L10,0 L10,10 Z");
    check_legacy_identical("M5,5 L45,5 L45,45 L5,45 Z");
    check_legacy_identical(RING_CCW);
    check_legacy_identical(RING_CW);
    check_legacy_identical("M0,0 L10,10 L10,0 L0,10 Z");
    check_legacy_identical("M2,3 L47,3 L47,40 L30,10 L20,45 L2,40 Z");
    /* 旧解析器多子路径时不重置 start 的历史行为也必须原样保留 */
    check_legacy_identical("M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z");

    /* --- 旧接口回归 --- */
    VPath lp;
    CK(svg_parse("M0,0 L10,0 L10,10 Z", &lp) == 0);
    CK(lp.count == 3);
    CK(svg_parse("L0,0", &lp) == -2);
    CK(svg_parse("M0,0 C1,2 3,4 5,6", &lp) == -3);
    CK(svg_parse("M0,0 L", &lp) == -4);
    CK(svg_parse("M0,0 L1,1 Z M2,2 L3,3 Z", &lp) == 0);
    CK(lp.count == 4);

    free(buf);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
