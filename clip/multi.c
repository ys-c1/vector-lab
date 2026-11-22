/* 多窗口顺序裁决驱动：
 *   multi --all <outdir>                跑全部内置用例（4 合法 + 2 非法）
 *   multi <scene.txt> <outdir> <name>   跑外部场景
 * scene.txt 格式：
 *   第一行：n（源多边形顶点数），随后 n 行 "x y"；
 *   再一行：m（窗口数），每个窗口先一行 wn 再 wn 行 "x y"。
 * 产物（写入 outdir）：
 *   <name>.verdict.txt   人读裁决：每排列合法性/原因/累积误差/单排列耗时，最优排列，全枚举耗时
 *   <name>.verdict.json  机读裁决
 *   <name>.stepdump.txt  最优排列的逐步面积（实际 P_k / 参照 S_k / 前缀交集 Q_k / e_k）
 *   summary.txt / summary.json  仅 --all 时生成
 */
#define _POSIX_C_SOURCE 199309L
#include "clip.h"
#include "cases.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static CPoly cpoly_from(const CPoint *pts, int n) {
    CPoly p;
    p.count = n < 64 ? n : 64;
    for (int i = 0; i < p.count; ++i) p.pts[i] = pts[i];
    return p;
}

static void write_poly(FILE *f, const CPoly *p) {
    fprintf(f, "%d\n", p->count);
    for (int i = 0; i < p->count; ++i)
        fprintf(f, "%.17g %.17g\n", p->pts[i].x, p->pts[i].y);
}

/* 重放给定排列，导出逐步实际/参照多边形与面积 */
static void dump_steps(const char *path, const CPoly *poly,
                       const CPoly *wins, int m, const int *order) {
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return; }
    CPoly pk = *poly;
    double cum = 0.0;
    for (int k = 0; k < m; ++k) {
        CPoly nxt;
        clip_convex(pk.pts, pk.count,
                    wins[order[k]].pts, wins[order[k]].count, &nxt);
        pk = nxt;
        CPoly qk, sk;
        clip_prefix_intersection(wins, order, k + 1, &qk);
        sk.count = 0;
        if (qk.count >= 3)
            clip_convex(poly->pts, poly->count, qk.pts, qk.count, &sk);
        double ap = clip_area_kahan(&pk), as = clip_area_kahan(&sk);
        double aq = clip_area_kahan(&qk);
        double e = ap - as; if (e < 0) e = -e;
        cum += e;
        fprintf(f, "step %d win %d err %.17g cum %.17g "
                "area_actual %.17g area_ref %.17g area_q %.17g\n",
                k + 1, order[k], e, cum, ap, as, aq);
        fprintf(f, "actual "); write_poly(f, &pk);
        fprintf(f, "ref ");    write_poly(f, &sk);
        fprintf(f, "q ");      write_poly(f, &qk);
    }
    fclose(f);
}

static const char *reason_name(int r) {
    switch (r) {
        case CLIP_REASON_EMPTY:  return "EMPTY_WINDOW";
        case CLIP_REASON_DUPWIN: return "DUPLICATE_WINDOW";
        default: return "OK";
    }
}

/* 跑一组用例，返回 0 正常；非法用例期望全部排列被拒 */
static int run_case(const CCase *c, const char *outdir,
                    FILE *summ_txt, FILE *summ_json, int json_first) {
    CPoly poly = cpoly_from(c->poly, c->pn);
    CPoly wins[CLIP_MAX_WINS];
    for (int i = 0; i < c->m; ++i)
        wins[i] = cpoly_from(c->win[i], c->wn[i]);

    static CVerdict v;
    int rc = clip_verdict(&poly, wins, c->m, &v);

    char path[1024];
    snprintf(path, sizeof path, "%s/%s.verdict.txt", outdir, c->name);
    FILE *ft = fopen(path, "w");
    snprintf(path, sizeof path, "%s/%s.verdict.json", outdir, c->name);
    FILE *fj = fopen(path, "w");
    if (!ft || !fj) { perror("open verdict"); return 2; }

    fprintf(ft, "case %s windows %d permutations %d rc %d\n",
            c->name, c->m, v.nperm, rc);
    fprintf(ft, "error model: E(pi)=sum_k |area(P_k)-area(S_k)|; "
            "P_k=chained clip, S_k=clip(poly, Q_k), Q_k=prefix window intersection\n");
    int nlegal = 0, nillegal = 0;
    for (int i = 0; i < v.nperm; ++i) {
        CPermResult *r = &v.res[i];
        fprintf(ft, "perm %d [", i);
        for (int k = 0; k < c->m; ++k)
            fprintf(ft, "%s%d", k ? "," : "", r->order[k]);
        fprintf(ft, "] ");
        if (r->legal) {
            ++nlegal;
            fprintf(ft, "LEGAL cum_err %.17g time_ns %.0f steps",
                    r->cum_err, r->time_ns);
            for (int k = 0; k < c->m; ++k)
                fprintf(ft, " %.3e", r->step_err[k]);
            fprintf(ft, "\n");
        } else {
            ++nillegal;
            if (r->reason == CLIP_REASON_EMPTY)
                fprintf(ft, "REJECT %s empty window index %d time_ns %.0f\n",
                        reason_name(r->reason), r->bad_a, r->time_ns);
            else
                fprintf(ft, "REJECT %s duplicate window indices %d,%d time_ns %.0f\n",
                        reason_name(r->reason), r->bad_a, r->bad_b, r->time_ns);
        }
    }

    /* 字典序排列（下标 0..m-1）的误差，用于减半对照 */
    double lex_err = 0.0; int lex_found = 0;
    for (int i = 0; i < v.nperm; ++i) {
        CPermResult *r = &v.res[i];
        int islex = 1;
        for (int k = 0; k < c->m; ++k) if (r->order[k] != k) islex = 0;
        if (islex && r->legal) { lex_err = r->cum_err; lex_found = 1; }
    }

    if (rc == 0) {
        fprintf(ft, "best permutation [");
        for (int k = 0; k < c->m; ++k)
            fprintf(ft, "%s%d", k ? "," : "", v.best_order[k]);
        fprintf(ft, "] best_cum_err %.17g\n", v.best_err);
        if (lex_found)
            fprintf(ft, "lexicographic cum_err %.17g ratio lex/best %.4f\n",
                    lex_err, v.best_err / lex_err);
        fprintf(ft, "total_enum_ns %.0f legal %d rejected %d\n",
                v.total_ns, nlegal, nillegal);

        snprintf(path, sizeof path, "%s/%s.stepdump.txt", outdir, c->name);
        dump_steps(path, &poly, wins, c->m, v.best_order);
    } else {
        fprintf(ft, "ALL PERMUTATIONS REJECTED legal %d rejected %d "
                "total_enum_ns %.0f\n", nlegal, nillegal, v.total_ns);
    }
    fclose(ft);

    /* JSON（手写，无外部依赖） */
    fprintf(fj, "{\n  \"case\": \"%s\",\n  \"windows\": %d,\n  \"permutations\": %d,\n",
            c->name, c->m, v.nperm);
    fprintf(fj, "  \"rc\": %d,\n  \"legal\": %d,\n  \"rejected\": %d,\n",
            rc, nlegal, nillegal);
    if (rc == 0) {
        fprintf(fj, "  \"best_order\": [");
        for (int k = 0; k < c->m; ++k) fprintf(fj, "%s%d", k ? "," : "", v.best_order[k]);
        fprintf(fj, "],\n  \"best_cum_err\": %.17g,\n", v.best_err);
        if (lex_found)
            fprintf(fj, "  \"lex_cum_err\": %.17g,\n  \"best_over_lex\": %.6f,\n",
                    lex_err, v.best_err / lex_err);
        fprintf(fj, "  \"total_enum_ns\": %.0f,\n", v.total_ns);
    }
    fprintf(fj, "  \"perms\": [\n");
    for (int i = 0; i < v.nperm; ++i) {
        CPermResult *r = &v.res[i];
        fprintf(fj, "    {\"perm\": [");
        for (int k = 0; k < c->m; ++k) fprintf(fj, "%s%d", k ? "," : "", r->order[k]);
        fprintf(fj, "], \"legal\": %s", r->legal ? "true" : "false");
        if (r->legal) {
            fprintf(fj, ", \"cum_err\": %.17g, \"time_ns\": %.0f, \"steps\": [",
                    r->cum_err, r->time_ns);
            for (int k = 0; k < c->m; ++k)
                fprintf(fj, "%s%.17g", k ? "," : "", r->step_err[k]);
            fprintf(fj, "]");
        } else {
            fprintf(fj, ", \"reason\": \"%s\", \"bad_a\": %d, \"bad_b\": %d",
                    reason_name(r->reason), r->bad_a, r->bad_b);
        }
        fprintf(fj, "}%s\n", i + 1 == v.nperm ? "" : ",");
    }
    fprintf(fj, "  ]\n}\n");
    fclose(fj);

    /* 汇总行 */
    if (c->expect_illegal) {
        int ok = (rc == -3 && nlegal == 0 && nillegal == v.nperm);
        fprintf(summ_txt, "%-10s REJECTED perms=%d reason=%s time_ns=%.0f expect_match=%s\n",
                c->name, v.nperm, reason_name(c->expect_reason),
                v.total_ns, ok ? "YES" : "NO");
        if (!json_first) fprintf(summ_json, ",\n");
        fprintf(summ_json,
                "  {\"case\": \"%s\", \"rc\": %d, \"rejected\": %d, \"reason\": \"%s\", "
                "\"total_enum_ns\": %.0f, \"expect_match\": %s}",
                c->name, rc, nillegal, reason_name(c->expect_reason),
                v.total_ns, ok ? "true" : "false");
        return ok ? 0 : 3;
    }

    int half = lex_found && v.best_err <= 0.5 * lex_err;
    fprintf(summ_txt,
            "%-10s m=%d perms=%d best=[", c->name, c->m, v.nperm);
    for (int k = 0; k < c->m; ++k)
        fprintf(summ_txt, "%s%d", k ? "," : "", v.best_order[k]);
    fprintf(summ_txt, "] best_err=%.3e lex_err=%.3e best/lex=%.4f (<=0.5: %s) "
            "total_ns=%.0f best_perm_ns=%.0f\n",
            v.best_err, lex_err, v.best_err / lex_err,
            half ? "YES" : "NO", v.total_ns, v.res[v.best].time_ns);
    if (!json_first) fprintf(summ_json, ",\n");
    fprintf(summ_json,
            "  {\"case\": \"%s\", \"m\": %d, \"perms\": %d, \"best_order\": [",
            c->name, c->m, v.nperm);
    for (int k = 0; k < c->m; ++k)
        fprintf(summ_json, "%s%d", k ? "," : "", v.best_order[k]);
    fprintf(summ_json,
            "], \"best_cum_err\": %.17g, \"lex_cum_err\": %.17g, "
            "\"best_over_lex\": %.6f, \"halved\": %s, "
            "\"total_enum_ns\": %.0f, \"best_perm_ns\": %.0f}",
            v.best_err, lex_err, v.best_err / lex_err,
            half ? "true" : "false", v.total_ns, v.res[v.best].time_ns);
    return half ? 0 : 4;
}

static int load_scene(const char *path, CPoly *poly, CPoly *wins, int *mout) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    int n;
    if (fscanf(f, "%d", &n) != 1 || n < 3 || n > 64) return -2;
    poly->count = n;
    for (int i = 0; i < n; ++i)
        if (fscanf(f, "%lf %lf", &poly->pts[i].x, &poly->pts[i].y) != 2) return -2;
    int m;
    if (fscanf(f, "%d", &m) != 1 || m < 1 || m > CLIP_MAX_WINS) return -3;
    for (int w = 0; w < m; ++w) {
        int wn;
        if (fscanf(f, "%d", &wn) != 1 || wn < 3 || wn > 64) return -4;
        wins[w].count = wn;
        for (int i = 0; i < wn; ++i)
            if (fscanf(f, "%lf %lf", &wins[w].pts[i].x, &wins[w].pts[i].y) != 2)
                return -4;
    }
    fclose(f);
    *mout = m;
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 3 && !strcmp(argv[1], "--all")) {
        const char *outdir = argv[2];
        char p[1024];
        snprintf(p, sizeof p, "%s/summary.txt", outdir);
        FILE *st = fopen(p, "w");
        snprintf(p, sizeof p, "%s/summary.json", outdir);
        FILE *sj = fopen(p, "w");
        if (!st || !sj) { perror("summary"); return 2; }
        fprintf(sj, "[\n");
        int rc = 0;
        for (int i = 0; i < NCASES; ++i) {
            int r = run_case(&CASES[i], outdir, st, sj, i == 0);
            if (r) rc = r;
        }
        fprintf(sj, "\n]\n");
        fclose(st); fclose(sj);
        return rc;
    }
    if (argc == 4) {
        CPoly poly, wins[CLIP_MAX_WINS]; int m;
        int rc = load_scene(argv[1], &poly, wins, &m);
        if (rc) { fprintf(stderr, "scene load error %d\n", rc); return 2; }
        CCase c;
        memset(&c, 0, sizeof c);
        c.name = argv[3];
        c.poly = poly.pts; c.pn = poly.count; c.m = m;
        c.expect_illegal = 0;
        for (int i = 0; i < m; ++i) { c.win[i] = wins[i].pts; c.wn[i] = wins[i].count; }
        char p[1024];
        snprintf(p, sizeof p, "%s/%s.summary.txt", argv[2], argv[3]);
        FILE *st = fopen(p, "w");
        snprintf(p, sizeof p, "%s/%s.summary.json", argv[2], argv[3]);
        FILE *sj = fopen(p, "w");
        if (!st || !sj) { perror("summary"); return 2; }
        fprintf(sj, "[\n");
        int r = run_case(&c, argv[2], st, sj, 1);
        fprintf(sj, "\n]\n");
        fclose(st); fclose(sj);
        return r == 4 ? 4 : 0;
    }
    fprintf(stderr,
        "usage: %s --all <outdir> | %s <scene.txt> <outdir> <name>\n",
        argv[0], argv[0]);
    return 1;
}
