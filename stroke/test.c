#include "stroke.h"
#include <stdio.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)
static int near(double a, double b, double e) { return fabs(a - b) < e; }

/* 手算：直角 L 形，宽 2。
 * 轮廓：(0,1) (0,-1) (9,0) (11,0) (9,10) (11,10)
 * 边 3=(11,0)-(9,10) 与边 5=(11,10)-(0,1) 交于 (297/32, 275/32)，
 * t = 55/64, u = 5/32；被绕入的三角形面积 = -45/32 = -1.40625。 */
static void test_hand_L(void) {
    SPoint p[3] = { {0,0}, {10,0}, {10,10} };
    SStrokeResult r;
    CK(stroke_resolve(p, 3, 2.0, 1, &r) == STROKE_OK);
    CK(r.hit_count == 1);
    CK(r.hits[0].pair == 0 && r.hits[0].ea == 3 && r.hits[0].eb == 5);
    CK(r.hits[0].kind == STROKE_HIT_PROPER);
    CK(near(r.hits[0].at.x, 297.0 / 32.0, 1e-12));
    CK(near(r.hits[0].at.y, 275.0 / 32.0, 1e-12));
    CK(near(r.hits[0].ta, 55.0 / 64.0, 1e-12));
    CK(near(r.hits[0].tb, 5.0 / 32.0, 1e-12));
    CK(near(r.raw_area, 55.0, 1e-12));
    CK(r.loop_count == 1);
    CK(r.loops[0].index == 1);
    CK(r.loops[0].pair == 0);
    CK(near(r.loops[0].area, -45.0 / 32.0, 1e-12));
    CK(near(r.kept_area, 56.40625, 1e-12));
    CK(near(r.kept_area + r.removed_area, r.raw_area, 1e-12));
}

/* 手算：四点 S 形，宽 2。两个交点，裁掉 4 点环面积 -28.599624... */
static void test_hand_S4(void) {
    SPoint p[4] = { {0,0}, {10,0}, {10,10}, {20,10} };
    SStrokeResult r;
    CK(stroke_resolve(p, 4, 2.0, 1, &r) == STROKE_OK);
    CK(r.hit_count == 2);
    CK(r.hits[0].pair == 0 && r.hits[0].ea == 3 && r.hits[0].eb == 5);
    CK(near(r.hits[0].at.x, 285.0 / 28.0, 1e-12));
    CK(near(r.hits[0].at.y, 253.0 / 28.0, 1e-12));
    CK(r.hits[1].pair == 1 && r.hits[1].ea == 3 && r.hits[1].eb == 7);
    CK(near(r.hits[1].at.x, 200.0 / 19.0, 1e-12));
    CK(near(r.hits[1].at.y, 99.0 / 19.0, 1e-12));
    CK(r.loop_count == 1);
    CK(r.loops[0].pair == 0);            /* 该环同时涉及 pair 0 和 pair 1 */
    CK(r.loops[0].hit_pair_count == 2);
    CK(r.loops[0].hit_pairs[0] == 0 && r.loops[0].hit_pairs[1] == 1);
    CK(near(r.loops[0].area, -28.599624060150376, 1e-9));
    CK(r.kept_count == 2);               /* 正向小三角与主环都保留 */
    CK(near(r.kept_area, 38.599624060150376, 1e-9));
    CK(near(r.raw_area, 10.0, 1e-12));
    CK(near(r.kept_area + r.removed_area, r.raw_area, 1e-12));
}

/* cut = 0 必须与 stroke_polyline 逐点一致；裁环不能改变这个出口。 */
static void test_no_cut_is_legacy(void) {
    SPoint cases[3][4] = {
        { {0,0}, {10,0}, {10,10}, {0,0} },
        { {0,0}, {10,0}, {10,10}, {20,10} },
        { {0,0}, {30,0}, {30,20}, {0,20} },
    };
    int ns[3] = { 3, 4, 4 };
    for (int c = 0; c < 3; ++c) {
        SPoly sp;
        SStrokeResult r;
        CK(stroke_polyline(cases[c], ns[c], 3.0, &sp) == 0);
        CK(stroke_resolve(cases[c], ns[c], 3.0, 0, &r) == STROKE_OK);
        CK(r.kept_count == 1);
        CK(r.kept[0].count == sp.count);
        int same = 1;
        for (int i = 0; i < sp.count; ++i) {
            if (r.points[r.kept[0].off + i].x != sp.pts[i].x ||
                r.points[r.kept[0].off + i].y != sp.pts[i].y) same = 0;
        }
        CK(same);
        CK(r.loop_count == 0);
        CK(r.points[r.kept[0].off + 0].x == sp.pts[0].x);
    }
}

/* 端点相交要报 -3 并指出对号。 */
static void test_endpoint_error(void) {
    SPoint p[4] = { {0,0}, {30,0}, {30,20}, {0,20} };  /* 缺失闭合边，端点相碰 */
    SStrokeResult r;
    CK(stroke_resolve(p, 4, 3.0, 1, &r) == STROKE_E_ENDPOINT);
    CK(r.status == STROKE_E_ENDPOINT);
    CK(r.bad_pair == 0);
    CK(r.degenerate == STROKE_HIT_ENDPOINT);
    CK(r.hit_count == 1);
    CK(r.hits[0].pair == 0);
    CK(r.hits[0].kind == STROKE_HIT_ENDPOINT);
    CK(r.hits[0].ea == 5 || r.hits[0].eb == 5);  /* 位于拐角外侧偏移边 */
    CK(r.loop_count == 0);
    /* cut = 0 时只记录退化对号，不阻断旧行为。 */
    CK(stroke_resolve(p, 4, 3.0, 0, &r) == STROKE_OK);
    CK(r.degenerate == STROKE_HIT_ENDPOINT);
    CK(r.bad_pair == 0);
}

/* 共线重叠要报 -4 并指出对号。 */
static void test_collinear_error(void) {
    /* 边 0=(0,0)-(10,0) 与边 5=(2,0)-(4,0) 共线重叠 2 个单位，且
     * 边 4 也共线但只碰到端点 (3,0)；按段对字典序，pair 0 为边 0-4 的
     * 端点命中，pair 1 为边 0-5 的共线重叠，故取最先出现的 pair 0 报错。 */
    SPoint p[7] = { {0,0}, {10,0}, {10,5}, {6,5}, {6,-1}, {2,0}, {4,0} };
    SStrokeResult r;
    int rc = stroke_resolve_polygon(p, 7, 1, &r);
    CK(rc == STROKE_E_ENDPOINT || rc == STROKE_E_COLLINEAR);
    CK(r.degenerate != 0);
    CK(r.bad_pair >= 0);
    CK(r.hits[r.bad_pair].kind != STROKE_HIT_PROPER);
    CK(r.hits[r.bad_pair].ea < r.hits[r.bad_pair].eb);
    /* 表里应能查到一个共线重叠对，用于定位。 */
    int found_collinear = 0;
    for (int i = 0; i < r.hit_count; ++i)
        if (r.hits[i].kind == STROKE_HIT_COLLINEAR) found_collinear = 1;
    CK(found_collinear);
}

static void test_args(void) {
    SStrokeResult r;
    SPoint p[3] = { {0,0}, {10,0}, {10,10} };
    CK(stroke_resolve_polygon(NULL, 3, 1, &r) == STROKE_E_ARG);
    CK(stroke_resolve_polygon(p, 2, 1, &r) == STROKE_E_ARG);
    CK(stroke_resolve(NULL, 3, 2.0, 1, &r) == STROKE_E_ARG);
    SPoint bad[2] = { {0,0}, {0,0} };
    CK(stroke_resolve(bad, 2, 1.0, 1, &r) == STROKE_E_ZERO_EDGE);
    CK(stroke_resolve(p, 1, 1.0, 1, &r) == STROKE_E_ARG);
    CK(stroke_resolve(p, 3, 0.0, 1, &r) == STROKE_E_ARG);
    /* 旧接口保持不变。 */
    SPoly sp;
    CK(stroke_polyline(p, 3, 2.0, &sp) == 0);
    CK(sp.count == 6);
    CK(stroke_polyline(p, 1, 2.0, &sp) == -1);
}

/* 两组基准：4 与 32 拐点，统计环数与面积闭合。 */
static int make_stair(int n_corner, SPoint *pts) {
    int n = n_corner + 2;
    pts[0] = (SPoint){0.0, 0.0};
    double x = 0.0, y = 0.0;
    int horiz = 1, dir = 1;
    for (int s = 0; s < n_corner + 1; ++s) {
        if (horiz) x += dir * 10.0;
        else { y += 4.0; dir = -dir; }
        horiz = !horiz;
        pts[s + 1] = (SPoint){x, y};
    }
    return n;
}

static void test_two_groups(void) {
    int groups[2] = { 4, 32 };
    int expect_loops[2] = { 1, 8 };
    int expect_kept[2] = { 3, 24 };
    SPoint pts[300];
    for (int g = 0; g < 2; ++g) {
        int n = make_stair(groups[g], pts);
        SStrokeResult r;
        CK(stroke_resolve(pts, n, 3.0, 1, &r) == STROKE_OK);
        CK(r.raw_count == 2 * n);
        CK(r.loop_count == expect_loops[g]);
        CK(r.kept_count == expect_kept[g]);
        CK(r.total_loop_count == r.kept_count + r.loop_count);
        CK(near(r.kept_area + r.removed_area, r.raw_area, 1e-8));
        CK(fabs(r.closure_error) < 1e-9);
        /* 所有交点都是 proper；环编号连续，对号可追溯。 */
        int all_proper = 1;
        for (int i = 0; i < r.hit_count; ++i)
            if (r.hits[i].kind != STROKE_HIT_PROPER) all_proper = 0;
        CK(all_proper);
        for (int i = 0; i < r.loop_count; ++i) {
            CK(r.loops[i].index == i + 1);
            CK(r.loops[i].pair >= 0 && r.loops[i].pair < r.hit_count);
            CK(r.loops[i].hit_pair_count >= 1);
            CK(r.loops[i].area * r.raw_area < 0.0);
            CK(r.loops[i].count >= 3);
        }
        /* 裁后每条保留环无残留自交。 */
        for (int k = 0; k < r.kept_count; ++k) {
            SPoint ring[STROKE_POOL_PTS];
            SStrokeRing *ring_info = &r.kept[k];
            for (int i = 0; i < ring_info->count; ++i)
                ring[i] = r.points[ring_info->off + i];
            SStrokeResult chk;
            CK(stroke_resolve_polygon(ring, ring_info->count, 0, &chk) == STROKE_OK);
            CK(chk.hit_count == 0);
        }
    }
}

int main(void) {
    test_hand_L();
    test_hand_S4();
    test_no_cut_is_legacy();
    test_endpoint_error();
    test_collinear_error();
    test_args();
    test_two_groups();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
