#include "sweep.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sets.h"

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

typedef struct { SwHit *v; int n; int cap; } HitArr;

static int collect(const SwSegSet *s, double W, unsigned flags, HitArr *out, SwStats *st) {
    int nh = 0;
    int rc = sweep_run(s->segs, s->n, W, flags, NULL, 0, &nh, st);
    if (rc != SW_OK) return rc;
    out->v = malloc(sizeof(SwHit) * (size_t)(nh ? nh : 1));
    out->cap = nh;
    out->n = 0;
    if (!out->v) return SW_E_NOMEM;
    rc = sweep_run(s->segs, s->n, W, flags, out->v, nh, &out->n, st);
    if (rc != SW_OK) { free(out->v); out->v = NULL; return rc; }
    return SW_OK;
}


static int same_hit(const SwHit *a, const SwHit *b) {
    return a->ia == b->ia && a->ib == b->ib && a->t == b->t &&
           a->u == b->u && a->x == b->x && a->y == b->y;
}

/* 记录逐个字段相同、且顺序相同（结构体有对齐填充，不能直接 memcmp）。 */
static int same_hit_sequence(HitArr *a, HitArr *b) {
    if (a->n != b->n) return 0;
    for (int i = 0; i < a->n; ++i)
        if (!same_hit(&a->v[i], &b->v[i])) return 0;
    return 1;
}

/* 去重后的 (ia, ib, 坐标) 多重集合相同：同一时刻多条事件造成的重复
 * 记录在合并 W=0 下会被折成一条，但交点集合必须完全一致。 */
static int pair_cmp(const void *pa, const void *pb) {
    const SwHit *a = (const SwHit *)pa, *b = (const SwHit *)pb;
    if (a->ia != b->ia) return a->ia < b->ia ? -1 : 1;
    if (a->ib != b->ib) return a->ib < b->ib ? -1 : 1;
    return 0;
}

static int same_unique_pairs(HitArr *a, HitArr *b) {
    if (a->n == 0 || b->n == 0) return a->n == 0 && b->n == 0;
    qsort(a->v, (size_t)a->n, sizeof(SwHit), pair_cmp);
    qsort(b->v, (size_t)b->n, sizeof(SwHit), pair_cmp);
    int i = 0, j = 0;
    while (i < a->n && j < b->n) {
        int c = pair_cmp(&a->v[i], &b->v[j]);
        if (c < 0) return 0;
        if (c > 0) return 0;
        /* 同一段对可能出现多次（不同事件时刻）——只要一侧出现过即算覆盖 */
        int ia = a->v[i].ia, ib = a->v[i].ib;
        while (i < a->n && a->v[i].ia == ia && a->v[i].ib == ib) ++i;
        while (j < b->n && b->v[j].ia == ia && b->v[j].ib == ib) ++j;
    }
    return i == a->n && j == b->n;
}

int main(void) {
    SwSegSet sets[4];
    swset_all(sets);

    /* ---- 参数与错误码 ---- */
    SwStats st;
    CK(sweep_run(sets[0].segs, 0, 0.0, 0u, NULL, 0, NULL, &st) == SW_E_ARG);
    CK(sweep_run(NULL, 2, 0.0, 0u, NULL, 0, NULL, &st) == SW_E_ARG);
    CK(sweep_run(sets[0].segs, sets[0].n, -1.0, SW_F_MERGE, NULL, 0, NULL, &st) == SW_E_WINDOW);
    CK(sweep_run(sets[0].segs, sets[0].n, NAN, SW_F_MERGE, NULL, 0, NULL, &st) == SW_E_WINDOW);
    CK(sweep_run(sets[0].segs, sets[0].n, 1.0, 8u, NULL, 0, NULL, &st) == SW_E_FLAGS);
    {
        SwSeg bad = {0, 0, INFINITY, 0};
        CK(sweep_run(&bad, 1, 0.0, SW_F_MERGE, NULL, 0, NULL, &st) == SW_E_SEG);
    }
    /* 负窗口在旧路径下被忽略 */
    CK(sweep_run(sets[0].segs, sets[0].n, -1.0, 0u, NULL, 0, NULL, &st) == SW_OK);

    /* ---- 时间差分布与零时间差事件 ---- */
    for (int k = 0; k < 4; ++k) {
        SwGap gaps[256];
        int ng = 0, zg = 0, zs = 0;
        CK(sweep_gap_dist(sets[k].segs, sets[k].n, gaps, 256, &ng, &zg, &zs) == SW_OK);
        int total = zg;
        for (int i = 0; i < ng; ++i) {
            CK(gaps[i].gap > 0.0);
            total += gaps[i].count;
        }
        CK(total == 2 * sets[k].n - 1);   /* 相邻时间差个数 = 事件数 - 1 */
        int zs2 = 0;
        for (int i = 0; i < sets[k].n; ++i)
            if (sets[k].segs[i].x0 == sets[k].segs[i].x1) ++zs2;
        CK(zs == zs2);
        /* 容量不足时报告 SW_E_CAP */
        if (ng > 0) {
            int dummy = 0;
            CK(sweep_gap_dist(sets[k].segs, sets[k].n, gaps, 1, &dummy, NULL, NULL) == SW_E_CAP
               || ng == 1);
        }
    }

    /* ---- 关闭新逻辑：与逐事件路径逐位一致 ---- */
    for (int k = 0; k < 4; ++k) {
        HitArr a = {0}, b = {0};
        SwStats sa, sb;
        CK(collect(&sets[k], 0.0, 0u, &a, &sa) == SW_OK);
        CK(collect(&sets[k], 999.0, 0u, &b, &sb) == SW_OK);
        CK(sa.cand_legacy == sa.cand_merged);
        CK(sa.groups == sa.times && sa.merge == 0);
        CK(same_hit_sequence(&a, &b));
        free(a.v); free(b.v);
    }

    /* ---- 合并窗口 W=0：去重后交点表与逐事件一致 ---- */
    for (int k = 0; k < 4; ++k) {
        HitArr leg = {0}, m0 = {0};
        SwStats sl, sm;
        CK(collect(&sets[k], 0.0, 0u, &leg, &sl) == SW_OK);
        CK(collect(&sets[k], 0.0, SW_F_MERGE | SW_F_AUDIT, &m0, &sm) == SW_OK);
        CK(sl.unique_hits == sm.unique_hits);
        CK(sm.safe == SW_SAFE);
        CK(sm.audit_pairs == 0);
        CK(same_unique_pairs(&leg, &m0));
        CK(sm.cand_merged == sm.cand_est);
        /* 独立估算接口只读时间差分布 + 活动计数，不枚举段对 */
        long long est = -1;
        CK(sweep_estimate_pairs(sets[k].segs, sets[k].n, 0.0, &est) == SW_OK);
        CK(est == sm.cand_merged);
        CK(sm.zero_segs == sl.zero_segs);
        free(leg.v); free(m0.v);
    }

    /* ---- 零长段单独统计 ---- */
    {
        HitArr x = {0};
        SwStats s;
        CK(collect(&sets[3], 0.0, SW_F_MERGE | SW_F_AUDIT, &x, &s) == SW_OK);
        CK(s.zero_segs > 0);
        CK(s.zero_buckets > 0);
        CK(s.cand_zero > 0);
        CK(s.hits_zero > 0);
        CK(s.hits_zero <= s.unique_hits);
        free(x.v);
    }
    /* 无零长段时单独统计必须为 0：水平段与“斜”竖直段都让 x 跨度非零 */
    {
        SwSegSet pure;
        int i = 0;
        for (int r = 0; r < 6; ++i, ++r)
            pure.segs[i] = (SwSeg){0.0, (double)r, 9.0, (double)r};
        for (int k = 0; k < 4; ++i, ++k)
            pure.segs[i] = (SwSeg){1.0 + 1.5 * k, -1.0, 1.2 + 1.5 * k, 7.0};
        pure.n = i;
        SwStats s;
        CK(sweep_legacy(pure.segs, pure.n, NULL, 0, NULL, &s) == SW_OK);
        CK(s.zero_segs == 0 && s.cand_zero == 0 && s.hits_zero == 0);
        SwStats m;
        CK(sweep_run(pure.segs, pure.n, 0.0, SW_F_MERGE, NULL, 0, NULL, &m) == SW_OK);
        CK(m.zero_segs == 0 && m.cand_zero == 0 && m.hits_zero == 0);
    }

    /* ---- 过大窗口必须被拒绝并说明 ---- */
    for (int k = 0; k < 4; ++k) {
        SwStats s;
        double span = 0.0;
        for (int i = 0; i < sets[k].n; ++i) {
            double lo = sets[k].segs[i].x0, hi = sets[k].segs[i].x1;
            if (hi > lo && hi > span) span = hi;
        }
        int rc = sweep_run(sets[k].segs, sets[k].n, span * 4.0 + 10.0,
                           SW_F_MERGE | SW_F_AUDIT, NULL, 0, NULL, &s);
        CK(rc == SW_OK);
        CK(s.safe == SW_UNSAFE);
        CK(s.reject_reason == SW_REJ_MISS);
        CK(s.miss_ia >= 0 && s.miss_ib > s.miss_ia);
        CK(s.miss_t1 >= s.miss_t0);
        char why[256];
        int len = sweep_format_reject(&s, why, sizeof why);
        CK(len > 0 && (int)strlen(why) == len);
        CK(strstr(why, "拒绝") != NULL);
    }

    /* ---- 反解窗口：四组都要达到“候选对降两成”且审计安全 ---- */
    for (int k = 0; k < 4; ++k) {
        SwStats leg;
        CK(sweep_legacy(sets[k].segs, sets[k].n, NULL, 0, NULL, &leg) == SW_OK);

        double W = -1.0;
        SwStats best;
        CK(sweep_choose(sets[k].segs, sets[k].n, 0.20,
                        SW_F_MERGE | SW_F_AUDIT, &W, &best) == SW_OK);
        CK(W >= 0.0);
        CK(best.safe == SW_SAFE);
        CK(best.merge == 1);
        CK(best.target_met == 1);
        CK(best.cand_merged <= (long long)(0.8 * (double)leg.cand_legacy));
        CK(best.audit_pairs >= 0);
        /* 估出的候选对必须与实际枚举一致（估计式不枚举段对） */
        CK(best.cand_est == best.cand_merged);
        long long est = -1;
        CK(sweep_estimate_pairs(sets[k].segs, sets[k].n, W, &est) == SW_OK);
        CK(est == best.cand_merged);
        /* 选定窗口下的实际交点必须覆盖全部真值交点 */
        HitArr got = {0};
        SwStats check;
        CK(collect(&sets[k], W, SW_F_MERGE | SW_F_AUDIT, &got, &check) == SW_OK);
        CK(check.safe == SW_SAFE);
        CK(check.unique_hits == leg.unique_hits);
        free(got.v);

        printf("组 %s：W=%.17g 候选 %lld→%lld（零长段 %lld），审计 %lld 对，"
               "交点 %d（零长段 %d）\n",
               sets[k].name, W, leg.cand_legacy, best.cand_merged,
               best.cand_zero, best.audit_pairs, best.unique_hits, best.hits_zero);
    }

    /* ---- 目标无法达到时如实报告 ---- */
    {
        double W = -1.0;
        SwStats best;
        CK(sweep_choose(sets[0].segs, sets[0].n, 0.999, SW_F_MERGE | SW_F_AUDIT,
                        &W, &best) == SW_OK);
        CK(best.target_met == 0);
    }

    /* ---- 交点表 CSV 行数一致 ---- */
    {
        HitArr x = {0};
        SwStats s;
        CK(collect(&sets[1], 0.0, SW_F_MERGE, &x, &s) == SW_OK);
        FILE *fp = tmpfile();
        CK(fp != NULL);
        if (fp) {
            CK(sweep_hits_csv(x.v, x.n, fp) == x.n);
            rewind(fp);
            int lines = 0, c;
            while ((c = fgetc(fp)) != EOF) if (c == '\n') ++lines;
            CK(lines == x.n);
            fclose(fp);
        }
        free(x.v);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
