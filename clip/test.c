#include "clip.h"
#include "cases.h"
#include <stdio.h>
#include <math.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* ---- 原有单窗裁剪检查，逐字保留 ---- */
    CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
    CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
    CPoly out;
    CK(clip_convex(poly,4,win,4,&out) == 0);
    CK(out.count == 4);
    CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);
    CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
    CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);
    CK(clip_convex(poly,2,win,4,&out) == -1);

#ifndef CLIP_NO_MULTI
    /* ---- 多窗口裁决检查 ---- */
    CK(clip_factorial(1) == 1 && clip_factorial(2) == 2 &&
       clip_factorial(3) == 6 && clip_factorial(4) == 24);

    static CVerdict v;
    for (int ci = 0; ci < NCASES; ++ci) {
        const CCase *c = &CASES[ci];
        CPoly sp = {.count = c->pn};
        for (int i = 0; i < c->pn; ++i) sp.pts[i] = c->poly[i];
        CPoly ws[CLIP_MAX_WINS];
        for (int i = 0; i < c->m; ++i) {
            ws[i].count = c->wn[i];
            for (int j = 0; j < c->wn[i]; ++j) ws[i].pts[j] = c->win[i][j];
        }
        int rc = clip_verdict(&sp, ws, c->m, &v);
        CK(v.nperm == (int)clip_factorial(c->m));

        if (c->expect_illegal) {
            CK(rc == -3 && v.best < 0);
            for (int i = 0; i < v.nperm; ++i) {
                CK(v.res[i].legal == 0);
                CK(v.res[i].reason == c->expect_reason);
                if (c->expect_reason == CLIP_REASON_EMPTY)
                    CK(v.res[i].bad_a >= 0);
                if (c->expect_reason == CLIP_REASON_DUPWIN)
                    CK(v.res[i].bad_a >= 0 && v.res[i].bad_b >= 0 &&
                       v.res[i].bad_a != v.res[i].bad_b);
            }
            continue;
        }

        CK(rc == 0 && v.best >= 0);
        /* 最优必须是所有合法排列里累积误差最小者；并列取最先出现（字典序优先） */
        for (int i = 0; i < v.nperm; ++i) {
            if (v.res[i].legal) CK(v.res[i].cum_err >= v.best_err);
        }
        /* 每步误差非负，累积误差等于逐步之和 */
        CPermResult *b = &v.res[v.best];
        double sum = 0.0;
        for (int k = 0; k < c->m; ++k) {
            CK(b->step_err[k] >= 0.0);
            sum += b->step_err[k];
        }
        CK(fabs(sum - b->cum_err) <= 1e-18 * (1.0 + fabs(b->cum_err)));

        /* 字典序排列误差；最优必须比它小一半以上 */
        for (int i = 0; i < v.nperm; ++i) {
            CPermResult *r = &v.res[i];
            int islex = 1;
            for (int k = 0; k < c->m; ++k) if (r->order[k] != k) islex = 0;
            if (islex && r->legal)
                CK(v.best_err <= 0.5 * r->cum_err);
        }

        /* 最优排列重放：逐步参照面积与前缀交集逻辑一致 */
        CPoly pk = sp, qacc = ws[b->order[0]];
        for (int k = 0; k < c->m; ++k) {
            CPoly nxt;
            CK(clip_convex(pk.pts, pk.count,
                           ws[b->order[k]].pts, ws[b->order[k]].count, &nxt) == 0);
            pk = nxt;
            if (k > 0) {
                CPoly qn;
                clip_convex(qacc.pts, qacc.count,
                            ws[b->order[k]].pts, ws[b->order[k]].count, &qn);
                qacc = qn;
            }
            CPoly sk;
            sk.count = 0;
            if (qacc.count >= 3)
                clip_convex(sp.pts, sp.count, qacc.pts, qacc.count, &sk);
            double e = fabs(clip_area_kahan(&pk) - clip_area_kahan(&sk));
            CK(fabs(e - b->step_err[k]) <= 1e-18 * (1.0 + e));
        }
    }

    /* 参数守卫 */
    CK(clip_verdict(NULL, NULL, 2, &v) == -1);
    CPoly dummy = {.count = 4, .pts = {{0,0},{1,0},{1,1},{0,1}}};
    CPoly dw[CLIP_MAX_WINS + 1];
    for (int i = 0; i <= CLIP_MAX_WINS; ++i) dw[i] = dummy;
    CK(clip_verdict(&dummy, dw, CLIP_MAX_WINS + 1, &v) == -2);
#endif

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
