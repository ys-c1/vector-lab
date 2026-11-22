#include "bezier.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)
#define NEAR(a,b,e) (fabs((a)-(b)) <= (e))

/* 旧演示曲线，用于逐字节不变的回归。 */
static const BPoint LEGACY_CURVE[4] = {{0,0},{20,40},{60,40},{80,0}};

int main(void) {
    BPoint c[4] = {{0,0},{0,10},{10,10},{10,0}};
    BPoint m = bezier_eval(c, 0.5);
    CK(m.x > 4.9 && m.x < 5.1);
    CK(m.y > 7.4 && m.y < 7.6);
    BPoly p;
    CK(bezier_flatten(c, 0.25, &p) == 0);
    CK(p.count == 9);
    CK(bezier_chord(c) > 9.9 && bezier_chord(c) < 10.1);
    CK(bezier_flatten(c, 0.0, &p) == -1);

    /* ---- 容差反解 ---- */
    CK(NEAR(bezier_tol_for_error(0.25), 1.0 / 3.0, 1e-15));
    CK(bezier_tol_for_error(0.0) < 0.0);
    CK(bezier_tol_for_error(-1.0) < 0.0);
    CK(bezier_tol_for_error(NAN) < 0.0);
    CK(bezier_tol_for_error(INFINITY) < 0.0);
    for (int i = 0; i < 40; ++i) {
        double E = pow(10.0, -6.0 + i * 0.25);
        double T = bezier_tol_for_error(E);
        CK(T > 0.0);
        CK(BEZ_CHORD_BOUND * T <= E);          /* 浮点下也不超过上界 */
        CK(E - BEZ_CHORD_BOUND * T <= 2e-16 * E); /* 且不浪费精度 */
    }

    /* ---- 自适应细分：每段都不超界 ---- */
    BPoint cls[4][4] = {
        {{0,0},{25,8},{75,8},{100,0}},
        {{0,0},{100,0},{100,100},{0,100}},
        {{0,0},{100,50},{0,-50},{100,0}},
        {{0,0},{33,0.2},{66,-0.2},{100,0}},
    };
    double es[4] = {0.5, 0.05, 0.1, 0.02};
    for (int k = 0; k < 4; ++k) {
        BFlat f;
        BPathResult r;
        CK(bezier_flatten_path((const BPoint (*)[4]) &cls[k], 1, es[k], &f, &r) ==
           BEZ_OK);
        CK(f.pieces >= 1);
        CK(NEAR(r.tol, bezier_tol_for_error(es[k]), 0.0));
        CK(bezier_verify_path((const BPoint (*)[4]) &cls[k], 1, &f, es[k], 2048,
                              &r) == BEZ_OK);
        CK(r.pass == 1);
        CK(r.violations == 0);
        CK(r.max_err <= es[k]);
        /* 端点必须落在曲线上 */
        CK(NEAR(f.pts[0].x, cls[k][0].x, 1e-12) && NEAR(f.pts[0].y, cls[k][0].y, 1e-12));
        CK(NEAR(f.pts[f.pieces].x, cls[k][3].x, 1e-12) &&
           NEAR(f.pts[f.pieces].y, cls[k][3].y, 1e-12));
        /* 参数区间首尾相接、覆盖 [0,1] */
        CK(f.t0[0] == 0.0 && f.t1[f.pieces - 1] == 1.0);
        for (int i = 1; i < f.pieces; ++i) CK(f.t0[i] == f.t1[i - 1]);
    }

    /* ---- 关掉新逻辑时逐字节不变 ---- */
    {
        BPoly old;
        BFlat f;
        BPathResult r;
        CK(bezier_flatten(LEGACY_CURVE, 0.5, &old) == 0);
        CK(bezier_flatten_mode(LEGACY_CURVE, 0.5, BEZ_MODE_LEGACY_FIXED, &f, &r) ==
           BEZ_OK);
        CK(f.pieces == old.count - 1);
        int same = 1;
        for (int i = 0; i < old.count; ++i)
            if (memcmp(&f.pts[i], &old.pts[i], sizeof(BPoint)) != 0) same = 0;
        CK(same);
        CK(bezier_flatten_mode(LEGACY_CURVE, 0.0, BEZ_MODE_LEGACY_FIXED, &f, &r) ==
           BEZ_E_TOL);
    }

    /* ---- 拒绝：负容差与退化控制点，指名段号 ---- */
    {
        BFlat f;
        BPathResult r;
        BPoint one[1][4] = {{{0,0},{10,10},{20,-10},{30,0}}};
        CK(bezier_flatten_path((const BPoint (*)[4]) one, 1, -1.0, &f, &r) ==
           BEZ_E_TOL);
        CK(r.bad_seg == -1);          /* 全局参数，不是某一段 */
        CK(bezier_flatten_path((const BPoint (*)[4]) one, 1, 0.0, &f, &r) ==
           BEZ_E_TOL);

        BPoint seg[3][4] = {
            {{0,0},{10,10},{20,-10},{30,0}},
            {{30,0},{40,10},{50,-10},{60,0}},
            {{60,0},{70,10},{NAN,0},{90,0}},      /* 段 2 退化 */
        };
        CK(bezier_flatten_path((const BPoint (*)[4]) seg, 3, 0.2, &f, &r) ==
           BEZ_E_DEGEN);
        CK(r.bad_seg == 2);

        BPoint seg1[2][4] = {
            {{0,0},{10,10},{20,-10},{30,0}},
            {{30,0},{40,10},{INFINITY,0},{60,0}}, /* 段 1 退化 */
        };
        CK(bezier_flatten_path((const BPoint (*)[4]) seg1, 2, 0.2, &f, &r) ==
           BEZ_E_DEGEN);
        CK(r.bad_seg == 1);

        /* 控制多边形收缩成一点 */
        BPoint zero[1][4] = {{{5,5},{5,5},{5,5},{5,5}}};
        CK(bezier_flatten_path((const BPoint (*)[4]) zero, 1, 0.2, &f, &r) ==
           BEZ_E_DEGEN);
        CK(r.bad_seg == 0);
    }

    /* ---- 数值上界：验证器本身能发现超界的折线 ---- */
    {
        BPoint line[4] = {{0,0},{0,50},{100,50},{100,0}};
        BFlat f;
        BPathResult r;
        /* 故意只切 1 段（把整条曲线当一条弦）：误差必然远超小上界。 */
        f.pieces = 1;
        f.pts[0] = line[0];
        f.pts[1] = line[3];
        f.t0[0] = 0.0;
        f.t1[0] = 1.0;
        f.seg[0] = 0;
        CK(bezier_verify_path((const BPoint (*)[4]) &line, 1, &f, 0.1, 2048, &r) ==
           BEZ_OK);
        CK(r.pass == 0);
        CK(r.violations == 1);
        CK(r.max_err > 37.0 && r.max_err <= 37.5); /* 解析弦高 = 3/4 * 50 */
    }

    /* ---- 多段路径：无重复顶点，段号正确 ---- */
    {
        BPoint seg[2][4] = {
            {{0,0},{10,10},{20,-10},{30,0}},
            {{30,0},{40,10},{50,-10},{60,0}},
        };
        BFlat f;
        BPathResult r;
        CK(bezier_flatten_path((const BPoint (*)[4]) seg, 2, 0.2, &f, &r) ==
           BEZ_OK);
        CK(bezier_verify_path((const BPoint (*)[4]) seg, 2, &f, 0.2, 2048, &r) ==
           BEZ_OK);
        CK(r.pass == 1);
        int dup = 0;
        for (int i = 1; i <= f.pieces; ++i)
            if (f.pts[i].x == f.pts[i - 1].x && f.pts[i].y == f.pts[i - 1].y)
                ++dup;
        CK(dup == 0);
        CK(f.seg[0] == 0 && f.seg[f.pieces - 1] == 1);
    }

    /* ---- 近直线曲线：一段 ----- */
    {
        BPoint line[4] = {{0,0},{33,0},{66,0},{100,0}};
        BFlat f;
        BPathResult r;
        CK(bezier_flatten_path((const BPoint (*)[4]) &line, 1, 0.01, &f, &r) ==
           BEZ_OK);
        CK(f.pieces == 1);
        CK(bezier_verify_path((const BPoint (*)[4]) &line, 1, &f, 0.01, 2048, &r) ==
           BEZ_OK);
        CK(r.pass == 1);
    }

    /* ---- 两步式：先反解容差，再按容差细分，结果与组合入口一致 ---- */
    {
        BPoint q[4] = {{0,0},{25,8},{75,8},{100,0}};
        double E = 0.25;
        double T = bezier_tol_for_error(E);
        BFlat a, b;
        BPathResult ra, rb;
        CK(bezier_flatten_tol((const BPoint (*)[4]) &q, 1, T, &a, &ra) == BEZ_OK);
        CK(bezier_flatten_path((const BPoint (*)[4]) &q, 1, E, &b, &rb) == BEZ_OK);
        CK(a.pieces == b.pieces);
        CK(memcmp(a.pts, b.pts, sizeof(BPoint) * (size_t)(a.pieces + 1)) == 0);
        CK(ra.tol == T);
        CK(NEAR(ra.err_bound, BEZ_CHORD_BOUND * T, 0.0));
        CK(rb.err_bound == E);
        CK(bezier_flatten_tol((const BPoint (*)[4]) &q, 1, 0.0, &a, &ra) ==
           BEZ_E_TOL);
        CK(ra.bad_seg == -1);
        CK(bezier_flatten_tol((const BPoint (*)[4]) &q, 1, -2.0, &a, &ra) ==
           BEZ_E_TOL);
        CK(bezier_flatten_tol((const BPoint (*)[4]) &q, 1, NAN, &a, &ra) ==
           BEZ_E_TOL);
        /* 容差取得过松时验证器必须报超界（证明“验证”这一步有效）。 */
        CK(bezier_flatten_tol((const BPoint (*)[4]) &q, 1, 5.0, &a, &ra) == BEZ_OK);
        CK(bezier_verify_path((const BPoint (*)[4]) &q, 1, &a, E, 2048, &ra) ==
           BEZ_OK);
        CK(ra.pass == 0 && ra.violations >= 1);
    }

    /* ---- 容量上限：超界时报错并指名段号 ---- */
    {
        BPoint big[1][4] = {{{0,0},{100,0},{100,100},{0,100}}};
        BFlat f;
        BPathResult r;
        CK(bezier_flatten_path((const BPoint (*)[4]) big, 1, 5e-5, &f, &r) ==
           BEZ_E_CAPACITY);
        CK(r.bad_seg == 0);
        CK(f.pieces == BEZ_MAX_PIECES);  /* 已写部分保留，便于定位 */
        CK(bezier_flatten_path((const BPoint (*)[4]) big, 1, 0.0005, &f, &r) ==
           BEZ_OK);
        CK(f.pieces == BEZ_MAX_PIECES);
        CK(bezier_verify_path((const BPoint (*)[4]) big, 1, &f, 0.0005, 4096,
                              &r) == BEZ_OK);
        CK(r.pass == 1);
    }

    /* ---- 参数校验 ---- */
    {
        BFlat f;
        BPathResult r;
        BPoint one[1][4] = {{{0,0},{10,10},{20,-10},{30,0}}};
        CK(bezier_flatten_path(NULL, 1, 0.2, &f, &r) == BEZ_E_ARG);
        CK(bezier_flatten_path((const BPoint (*)[4]) one, 0, 0.2, &f, &r) ==
           BEZ_E_ARG);
        CK(bezier_flatten_path((const BPoint (*)[4]) one, 1, 0.2, NULL, &r) ==
           BEZ_E_ARG);
        CK(bezier_verify_path((const BPoint (*)[4]) one, 1, &f, 0.2, 2048, NULL) ==
           BEZ_E_ARG);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
