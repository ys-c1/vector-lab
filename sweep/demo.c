#define _POSIX_C_SOURCE 199309L
#include "sweep.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "sets.h"

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static double repeat_ms(const SwSegSet *s, double W, unsigned flags, int reps,
                        SwStats *st) {
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        SwStats tmp;
        double t0 = now_ms();
        int rc = sweep_run(s->segs, s->n, W, flags, NULL, 0, NULL, &tmp);
        double dt = now_ms() - t0;
        if (rc != SW_OK) return -1.0;
        if (dt < best) best = dt;
        if (r == 0 && st) *st = tmp;
    }
    return best;
}

int main(int argc, char **argv) {
    const char *csv_path = argc > 1 ? argv[1] : "build/sweep_groups.csv";
    const char *hit_path = argc > 2 ? argv[2] : "build/sweep_hits.csv";
    const char *legacy_path = argc > 3 ? argv[3] : "build/sweep_legacy_hits.csv";
    const char *win_path = argc > 4 ? argv[4] : "build/sweep_windows.csv";

    SwSegSet sets[4];
    swset_all(sets);

    FILE *csv = fopen(csv_path, "w");
    if (!csv) { perror(csv_path); return 1; }
    FILE *hits = fopen(hit_path, "w");
    if (!hits) { perror(hit_path); fclose(csv); return 1; }
    FILE *lghits = fopen(legacy_path, "w");
    if (!lghits) { perror(legacy_path); fclose(csv); fclose(hits); return 1; }
    FILE *win = fopen(win_path, "w");
    if (!win) { perror(win_path); fclose(csv); fclose(hits); fclose(lghits); return 1; }
    fprintf(win, "set,window,est_cand,cand,audit_pairs,total,safe,reason\n");

    fprintf(csv, "set,n,zero_segs,zero_gap_events,events,times,"
                 "legacy_cand,window,groups,cand,zero_cand,audit_pairs,total,"
                 "drop_pct,est,est_drop_pct,safe,target_met,unique_hits,zero_hits,"
                 "legacy_ms,merged_ms\n");

    printf("四组线段集：事件时间差分布 → 合并窗口下的候选对\n\n");
    printf("%-22s %4s %6s %8s %8s %8s %10s %10s %8s %9s %9s %7s\n",
           "组", "n", "零长段", "零差事件", "事件数", "不同时刻",
           "逐事件候选", "合并候选", "下降%", "估候选", "估下降%", "审计对");
    printf("-----------------------------------------------------------------------------------------------------------\n");

    int bad = 0;
    typedef struct { const char *name; double W; long long leg, mrg, zc; double tl, tm; int ok; } SumRow;
    SumRow sum[4];
    for (int k = 0; k < 4; ++k) {
        const SwSegSet *s = &sets[k];

        /* 时间差分布：不同正间隔 + 零间隔计数 */
        SwGap gaps[512];
        int ng = 0, zg = 0, zs = 0;
        if (sweep_gap_dist(s->segs, s->n, gaps, 512, &ng, &zg, &zs) != SW_OK) {
            printf("gap_dist failed\n");
            return 2;
        }
        printf("\n[%s] n=%d，零长段 %d，零时间差事件 %d，不同正间隔 %d 种：\n",
               s->name, s->n, zs, zg, ng);
        printf("  正间隔：");
        for (int i = 0; i < ng && i < 10; ++i)
            printf("%g×%d ", gaps[i].gap, gaps[i].count);
        if (ng > 10) printf("…（后略）");
        printf("\n");

        SwStats leg;
        if (sweep_legacy(s->segs, s->n, NULL, 0, NULL, &leg) != SW_OK) return 2;

        /* 逐档窗口：只看时间差分布估候选，与实际枚举/审计对照 */
        printf("  窗口候选（来自时间差分布）：\n");
        printf("    %-20s %10s %10s %8s %7s %6s %s\n",
               "W", "估候选", "实际候选", "审计对", "总代价", "安全", "说明");
        for (int i = 0; i <= ng; ++i) {
            double Wc = (i == 0) ? 0.0 : gaps[i-1].gap;
            long long est = -1;
            if (sweep_estimate_pairs(s->segs, s->n, Wc, &est) != SW_OK) continue;
            SwStats sc;
            if (sweep_run(s->segs, s->n, Wc, SW_F_MERGE | SW_F_AUDIT,
                          NULL, 0, NULL, &sc) != SW_OK) continue;
            printf("    %-20.17g %10lld %10lld %8lld %7lld %6s %s\n",
                   Wc, est, sc.cand_merged, sc.audit_pairs,
                   sc.cand_merged + sc.audit_pairs,
                   sc.safe == SW_SAFE ? "safe" : (sc.safe == SW_UNSAFE ? "拒绝" : "-"),
                   sc.safe == SW_UNSAFE ? "过大窗口漏交" : "");
            fprintf(win, "%s,%.17g,%lld,%lld,%lld,%lld,%s,%s\n",
                    s->name, Wc, est, sc.cand_merged, sc.audit_pairs,
                    sc.cand_merged + sc.audit_pairs,
                    sc.safe == SW_SAFE ? "safe" : (sc.safe == SW_UNSAFE ? "unsafe" : "unknown"),
                    sc.safe == SW_UNSAFE ? "leak" : "");
            if (i > 40) { printf("    …（后略）\n"); break; }
        }

        /* 反解窗口 */
        double W = -1.0;
        SwStats best;
        int rc = sweep_choose(s->segs, s->n, 0.20, SW_F_MERGE | SW_F_AUDIT, &W, &best);
        if (rc != SW_OK) { printf("sweep_choose failed: %s\n", sweep_strerror(rc)); bad = 1; continue; }

        double drop = 1.0 - (double)best.cand_merged / (double)leg.cand_legacy;
        double edrop = 1.0 - (double)best.cand_est / (double)leg.cand_legacy;

        double t_leg = repeat_ms(s, 0.0, 0u, 64, NULL);
        double t_mrg = repeat_ms(s, W, SW_F_MERGE | SW_F_AUDIT, 64, NULL);

        printf("\n  最优窗口 W=%.17g（%d 组；%d 个不同时刻 → %d 组）\n",
               W, best.groups, leg.times, best.groups);
        printf("  候选对：逐事件 %lld → 合并 %lld（下降 %.1f%%），"
               "其中零长段 %lld\n",
               leg.cand_legacy, best.cand_merged, drop * 100.0, best.cand_zero);
        long long pure_est = -1;
        if (sweep_estimate_pairs(s->segs, s->n, W, &pure_est) != SW_OK) pure_est = -1;
        printf("  仅由时间差分布+活动计数估出 %lld（下降 %.1f%%，与实测%s）\n",
               pure_est, (1.0 - (double)pure_est / (double)leg.cand_legacy) * 100.0,
               pure_est == best.cand_merged ? "一致" : "不一致");
        if (pure_est != best.cand_merged) bad = 1;
        (void)edrop;
        printf("  审计风险段对 %lld，交点 %d（去重；零长段 %d）；安全=%s，达到两成下降=%s\n",
               best.audit_pairs, best.unique_hits, best.hits_zero,
               best.safe == SW_SAFE ? "是" : "否", best.target_met ? "是" : "否");
        printf("  耗时（64 次取最小）：逐事件 %.3f ms，合并 %.3f ms\n", t_leg, t_mrg);

        /* 过大窗口：拒绝并说明 */
        double span = 0.0;
        for (int i = 0; i < s->n; ++i) {
            double lo = s->segs[i].x0, hi = s->segs[i].x1;
            if (hi - lo > span) span = hi - lo;
        }
        double bigW = span * 4.0 + 10.0;
        SwStats big;
        if (sweep_run(s->segs, s->n, bigW, SW_F_MERGE | SW_F_AUDIT, NULL, 0, NULL, &big) != SW_OK) {
            printf("  过大窗口检查失败\n");
            bad = 1;
        } else {
            char why[512];
            sweep_format_reject(&big, why, sizeof why);
            printf("  过大窗口 W=%g：%s\n", bigW, why);
            if (big.safe != SW_UNSAFE) { printf("  预期拒绝但未拒绝\n"); bad = 1; }
        }

        printf("\n");
        printf("%-22s %4d %6d %8d %8d %8d %10lld %10lld %7.1f%% %9lld %8.1f%% %7lld\n",
               s->name, s->n, best.zero_segs, best.zero_gap_events, best.events,
               best.times, leg.cand_legacy, best.cand_merged, drop * 100.0,
               best.cand_est, edrop * 100.0, best.audit_pairs);

        fprintf(csv, "%s,%d,%d,%d,%d,%d,%lld,%g,%d,%lld,%lld,%lld,%lld,%.4f,"
                     "%lld,%.4f,%s,%d,%d,%d,%.4f,%.4f\n",
                s->name, s->n, best.zero_segs, best.zero_gap_events, best.events,
                best.times, leg.cand_legacy, W, best.groups, best.cand_merged,
                best.cand_zero, best.audit_pairs,
                best.cand_merged + best.audit_pairs, drop * 100.0,
                best.cand_est, edrop * 100.0,
                best.safe == SW_SAFE ? "safe" : "unsafe", best.target_met,
                best.unique_hits, best.hits_zero, t_leg, t_mrg);

        /* 选定窗口下的交点表（去重前，snap=组号）导出 */
        int nh = 0;
        SwStats hs;
        if (sweep_run(s->segs, s->n, W, SW_F_MERGE | SW_F_AUDIT, NULL, 0, &nh, &hs) != SW_OK)
            return 2;
        SwHit *hv = malloc(sizeof(SwHit) * (size_t)(nh ? nh : 1));
        if (!hv) return 2;
        if (sweep_run(s->segs, s->n, W, SW_F_MERGE | SW_F_AUDIT, hv, nh, &nh, &hs) != SW_OK)
            { free(hv); return 2; }
        fprintf(hits, "# set=%s window=%.17g groups=%d\n", s->name, W, hs.groups);
        sweep_hits_csv(hv, nh, hits);
        free(hv);

        /* 同一组的逐事件交点表（关掉新逻辑时的基准输出） */
        int nl = 0;
        SwStats ls;
        if (sweep_run(s->segs, s->n, W, 0u, NULL, 0, &nl, &ls) != SW_OK) return 2;
        SwHit *lv = malloc(sizeof(SwHit) * (size_t)(nl ? nl : 1));
        if (!lv) return 2;
        if (sweep_run(s->segs, s->n, W, 0u, lv, nl, &nl, &ls) != SW_OK)
            { free(lv); return 2; }
        fprintf(lghits, "# set=%s window=%.17g groups=%d\n", s->name, W, ls.groups);
        sweep_hits_csv(lv, nl, lghits);
        free(lv);

        if (!best.target_met || best.safe != SW_SAFE) bad = 1;
        if (best.cand_merged > (long long)(0.8 * (double)leg.cand_legacy)) bad = 1;

        sum[k].name = s->name;
        sum[k].W = W;
        sum[k].leg = leg.cand_legacy;
        sum[k].mrg = best.cand_merged;
        sum[k].zc = best.cand_zero;
        sum[k].tl = t_leg;
        sum[k].tm = t_mrg;
        sum[k].ok = (best.target_met && best.safe == SW_SAFE);
    }
    fclose(csv);
    fclose(hits);
    fclose(lghits);
    fclose(win);
    printf("\n明细已导出：%s（每组一行），%s（选定窗口下的交点表），"
           "%s（逐事件交点表），%s（逐档窗口）\n",
           csv_path, hit_path, legacy_path, win_path);

    printf("\n汇总（四组线段集各一行）：\n");
    printf("%-22s %12s %10s %10s %8s %10s %10s %6s\n",
           "组", "合并窗口", "候选(逐事件)", "候选(合并)", "零长段", "下降%", "耗时μs", "达标");
    for (int k = 0; k < 4; ++k) {
        double drop = 1.0 - (double)sum[k].mrg / (double)sum[k].leg;
        printf("%-22s %12.6g %10lld %10lld %8lld %9.1f%% %6.1f→%-6.1f %6s\n",
               sum[k].name, sum[k].W, sum[k].leg, sum[k].mrg, sum[k].zc,
               drop * 100.0, sum[k].tl * 1000.0, sum[k].tm * 1000.0,
               sum[k].ok ? "是" : "否");
    }
    return bad ? 1 : 0;
}
