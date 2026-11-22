/* 容差反解：不用固定阈值，而是从数据反推。
 *
 * 真值来自 gold/ref 的精确整数参照（master 设计几何），gap 由 double 管线
 * 在加噪输入 segs 上逐对计算。设
 *   eps_req = max(gap over 真值对)          —— 漏判为零的数学下界
 *   cap     = 0.1 * min(非退化段长度)        —— 线长一成红线
 * 扫描从 1e-15 起按半对数网格（每 10 倍 40 档）单调上升，取第一个漏判为 0
 * 的网格点，再在 [上一档, 该档] 内二分 200 次收敛到 eps_req，从而证明：
 *   - eps < eps_req 时至少一个真值对的 gap > eps（绑定对），必漏；
 *   - eps = eps_req 时全部真值对满足 gap <= eps，漏判为零。
 * eps_req > cap 则拒绝该数据集并说明。零长（退化）段全程单独统计。
 *
 * 用法：sweep <segs.txt> <gold.out>
 */
#define _POSIX_C_SOURCE 199309L
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXN 300
#define MAXP (MAXN * MAXN)
#define GRID_PER_DECADE 40
#define GRID_LO 1e-15
#define GRID_HI 1e2
#define BISECT_ITERS 200
#define TIME_REPS 50

typedef struct { int i, j, truth, zero; double gap; } Pair;

static int load_segs(const char *path, IPoint *a0, IPoint *a1, int *zero_out) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen segs"); return -1; }
    int n = 0, z = 0;
    while (n < MAXN && fscanf(f, "%lf %lf %lf %lf",
                              &a0[n].x, &a0[n].y, &a1[n].x, &a1[n].y) == 4) {
        if (isect_zero_len(a0[n], a1[n])) ++z;
        ++n;
    }
    fclose(f);
    *zero_out = z;
    return n;
}

static int load_gold(const char *path, int n, char *truth) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen gold"); return -1; }
    int nparsed = 0, tparsed = 0, zparsed = -1;
    long long snapv = 0;
    int hdr = fscanf(f, "n=%d truth=%d zero=%d snap=%lld",
                     &nparsed, &tparsed, &zparsed, &snapv);
    if (hdr < 3) { fprintf(stderr, "bad gold header\n"); return -1; }
    if (nparsed != n) { fprintf(stderr, "n mismatch: segs=%d gold=%d\n", n, nparsed); return -1; }
    int i, j, got = 0;
    while (fscanf(f, "%d %d", &i, &j) == 2) {
        if (i < 0 || j < 0 || i >= n || j >= n || i >= j) {
            fprintf(stderr, "bad gold pair %d %d\n", i, j); return -1;
        }
        truth[(size_t)i * n + j] = 1;
        ++got;
    }
    if (got != tparsed) { fprintf(stderr, "truth count mismatch\n"); return -1; }
    return zparsed;
}

/* 退化清单：设计即零长的段下标。文件缺失时回退到坐标相等判定。 */
static void load_manifest(const char *segs_path, int n, char *zflag) {
    char path[1024];
    const char *slash = strrchr(segs_path, '/');
    size_t dlen = slash ? (size_t)(slash - segs_path) + 1 : 0;
    snprintf(path, sizeof path, "%.*smanifest.txt", (int)dlen, segs_path);
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "manifest 缺失（%s），退化身份回退为坐标相等判定\n", path);
        return;
    }
    int idx, got = 0;
    while (fscanf(f, "%d", &idx) == 1) {
        if (idx < 0 || idx >= n) { fprintf(stderr, "bad manifest idx %d\n", idx); exit(5); }
        zflag[idx] = 1; ++got;
    }
    fclose(f);
    fprintf(stderr, "manifest %s: designed_zero=%d\n", path, got);
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "usage: %s <segs.txt> <gold.out>\n", argv[0]); return 1; }
    static IPoint a0[MAXN], a1[MAXN];
    static char zflag[MAXN];
    int coord_zero = 0;
    int n = load_segs(argv[1], a0, a1, &coord_zero);
    if (n < 2) return 2;
    load_manifest(argv[1], n, zflag);
    int zero_segs = 0;
    for (int i = 0; i < n; ++i) if (zflag[i]) ++zero_segs;

    static char truth[MAXP];
    int gold_zero = load_gold(argv[2], n, truth);
    if (gold_zero < 0) return 3;
    if (gold_zero != zero_segs) {
        fprintf(stderr, "WARN 退化数 manifest=%d gold=%d 坐标相等=%d\n",
                zero_segs, gold_zero, coord_zero);
    }

    /* 全部点对预算一次：gap、真值标志、退化标志 */
    static Pair pr[MAXP];
    int np = 0, truth_cnt = 0, zero_pairs = 0, zero_truth = 0;
    double min_len = 1.0 / 0.0;
    for (int i = 0; i < n; ++i) {
        if (!zflag[i]) {
            double L = hypot(a1[i].x - a0[i].x, a1[i].y - a0[i].y);
            if (L < min_len) min_len = L;
        }
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            Pair *p = &pr[np++];
            p->i = i; p->j = j;
            p->truth = truth[(size_t)i * n + j];
            p->zero = zflag[i] || zflag[j];
            p->gap = isect_gap(a0[i], a1[i], a0[j], a1[j]);
            if (p->truth) ++truth_cnt;
            if (p->zero) ++zero_pairs;
            if (p->zero && p->truth) ++zero_truth;
        }

    /* 解析需求与红线 */
    double eps_req = 0.0;
    int bind_i = -1, bind_j = -1, bind_zero = 0;
    for (int k = 0; k < np; ++k)
        if (pr[k].truth && pr[k].gap >= eps_req) {
            eps_req = pr[k].gap; bind_i = pr[k].i; bind_j = pr[k].j;
            bind_zero = pr[k].zero;
        }
    double cap = 0.1 * min_len;
    int rejected = eps_req > cap;

    /* 网格扫描：从最小档开始，第一个漏判为 0 的档位即网格答案 */
    double t0 = now_sec();
    double eps_grid = 0.0, prev_grid = GRID_LO;
    int grid_misses = -1, grid_cands = 0, grid_steps = 0;
    int decades = (int)ceil((log10(GRID_HI) - log10(GRID_LO)) * GRID_PER_DECADE) + 1;
    for (int s = 0; s <= decades; ++s) {
        double eps = GRID_LO * pow(10.0, (double)s / GRID_PER_DECADE);
        if (eps > GRID_HI) eps = GRID_HI;
        int misses = 0, cands = 0;
        for (int k = 0; k < np; ++k) {
            int hit = pr[k].gap <= eps;
            if (hit) ++cands;
            if (pr[k].truth && !hit) ++misses;
        }
        ++grid_steps;
        if (misses == 0) { eps_grid = eps; grid_misses = 0; grid_cands = cands; break; }
        prev_grid = eps;
    }
    double t_grid = now_sec() - t0;

    /* 二分：在 (prev_grid, eps_grid] 内收敛到最小零漏判容差 */
    double lo = prev_grid, hi = eps_grid;
    for (int it = 0; it < BISECT_ITERS; ++it) {
        double mid = 0.5 * (lo + hi);
        int misses = 0;
        for (int k = 0; k < np; ++k)
            if (pr[k].truth && pr[k].gap > mid) ++misses;
        if (misses == 0) hi = mid; else lo = mid;
    }
    double eps_star = hi;
    double t_total = t_grid; (void)t_total;

    /* 最终口径：反解容差处的漏判与候选（必须漏判 0） */
    int miss_star = 0, cand_star = 0, cand_zero = 0, fp = 0;
    for (int k = 0; k < np; ++k) {
        int hit = pr[k].gap <= eps_star;
        if (hit) {
            ++cand_star;
            if (pr[k].zero) ++cand_zero;
            if (!pr[k].truth) ++fp;
        }
        if (pr[k].truth && !hit) ++miss_star;
    }

    /* 「再小就漏」证明：绑定对 gap == eps_req，把容差下调一个 ULP 后，
     * 该对必然落到容差外 -> 漏判 >= 1。分别从解析口径与扫描口径验证。 */
    double eps_below = nextafter(eps_req, 0.0);
    int bind_included = 0, bind_missed = 0, miss_below = 0;
    for (int k = 0; k < np; ++k) {
        if (pr[k].truth && pr[k].gap > eps_below) ++miss_below;
        if (pr[k].i == bind_i && pr[k].j == bind_j) {
            bind_included = pr[k].gap <= eps_req;
            bind_missed = pr[k].gap > eps_below;
        }
    }

    /* 耗时：对完整扫描（网格+二分）重复计时取均值，毫秒 */
    double best = 1e30;
    for (int rep = 0; rep < TIME_REPS; ++rep) {
        double a = now_sec();
        double l2 = prev_grid, h2 = eps_grid;
        for (int s = 0; s < grid_steps; ++s) {
            double eps = GRID_LO * pow(10.0, (double)s / GRID_PER_DECADE);
            int misses = 0;
            for (int k = 0; k < np; ++k)
                if (pr[k].truth && pr[k].gap > eps) ++misses;
            if (misses == 0) break;
        }
        for (int it = 0; it < BISECT_ITERS; ++it) {
            double mid = 0.5 * (l2 + h2);
            int misses = 0;
            for (int k = 0; k < np; ++k)
                if (pr[k].truth && pr[k].gap > mid) ++misses;
            if (misses == 0) h2 = mid; else l2 = mid;
        }
        double dt = now_sec() - a;
        if (dt < best) best = dt;
    }

    printf("n=%d pairs=%d truth=%d zero_segs=%d zero_pairs=%d zero_truth=%d\n",
           n, np, truth_cnt, zero_segs, zero_pairs, zero_truth);
    printf("eps_req=%.9e eps_star=%.9e grid_eps=%.3e grid_steps=%d\n",
           eps_req, eps_star, eps_grid, grid_steps);
    printf("bind_pair=(%d,%d) bind_gap=%.9e bind_zero=%d\n",
           bind_i, bind_j, eps_req, bind_zero);
    printf("min_seg_len=%.9e cap_10pct=%.9e rejected=%d\n",
           min_len, cap, rejected);
    printf("misses=%d candidates=%d candidate_zero_pairs=%d false_candidates=%d\n",
           miss_star, cand_star, cand_zero, fp);
    printf("grid_first_zero_misses=%d grid_candidates=%d\n", grid_misses, grid_cands);
    printf("proof_below: eps_below=%.9e bind_included_at_req=%d bind_missed_below=%d misses_below=%d\n",
           eps_below, bind_included, bind_missed, miss_below);
    if (!(bind_included && bind_missed && miss_below >= 1))
        fprintf(stderr, "WARN 「再小就漏」边界证明未通过\n");
    printf("time_scan_best_ms=%.4f time_gap_precompute_ms=%.4f\n",
           best * 1e3, t_grid * 1e3);
    /* 全精度机器可读结果，供 table 直接取用，避免报告文本 round-trip 丢精度 */
    char rpath[1024];
    const char *slash = strrchr(argv[1], '/');
    size_t dlen = slash ? (size_t)(slash - argv[1]) + 1 : 0;
    snprintf(rpath, sizeof rpath, "%.*ssweep_result.txt", (int)dlen, argv[1]);
    FILE *rf = fopen(rpath, "w");
    if (rf) {
        fprintf(rf, "eps_star=%.17g\nmisses=%d\ncandidates=%d\n",
                eps_star, miss_star, cand_star);
        fclose(rf);
    }
    if (rejected)
        printf("VERDICT=REJECT eps_req 超过最短非退化段长度的 10%%（%.3e > %.3e），"
               "吸附容差不可接受\n", eps_req, cap);
    else
        printf("VERDICT=ACCEPT 反解容差 %.3e <= 一成红线 %.3e，漏判为 0\n", eps_star, cap);
    return miss_star != 0 ? 4 : 0;
}
