/* 剥层验证驱动：逐层剥凸层，每层多边形与面积单独落盘，和 gold/ref
 * 的另一种面积算法比对；最后反解“覆盖 95% 面积要剥到第几层”。
 *
 * 用法：
 *   run one    <pts.txt> <outdir> [ref]
 *   run points <name> [ref]     场景：random/collinear/dup/rings
 *   run cover  <area1> <area2> ... <frac>   反解最小层号（最后一个参数是比例）
 *
 * 点集文件每行 "x y"。每层产物：
 *   <outdir>/layers/L01_rem.txt     进入本层的去重剩余点
 *   <outdir>/layers/L01_hull.txt    本层凸包（严格顶点，逆时针）
 *   <outdir>/gold/L01_hull.txt      同一凸包（交给参照）
 *   <outdir>/gold/L01_area.txt      参照算出的面积
 *   <outdir>/report.tsv             汇总（含参照面积与相对差）
 */
#define _POSIX_C_SOURCE 200809L
#include "peel.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PI 3.14159265358979323846
#define TOL_REL 1e-4   /* 层面积与参照的相对差异上限：万分之一 */
#define TOL_ABS 1e-6
#define COVER_FRAC 0.95

typedef struct {
    const char *outdir;
    const char *ref;   /* gold 二进制路径，NULL 表示不比 */
    FILE *rpt;
    double cum_rel;    /* 逐层相对偏差累计 */
    double worst_rel;
    int worst_layer;
    double worst_gross_rel; /* 辅参照（256x256 数格子）与主参照的相对差 */
    int worst_gross_layer;
    int failed;
    double gold[PEEL_MAX_LAYERS];
    int has_gold[PEEL_MAX_LAYERS];
    double gross[PEEL_MAX_LAYERS];
    int has_gross[PEEL_MAX_LAYERS];
    char err[1024];
} DRun;

static int load_pts(const char *path, PPoint *pts, int cap, char *err, size_t errsz) {
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, errsz, "无法打开 %s", path); return -1; }
    int n = 0;
    double x, y;
    int r;
    while ((r = fscanf(f, "%lf %lf", &x, &y)) == 2) {
        if (n >= cap) { snprintf(err, errsz, "%s 点数超过上限 %d", path, cap); fclose(f); return -1; }
        pts[n].x = x; pts[n].y = y; ++n;
    }
    if (r != EOF) { snprintf(err, errsz, "%s 第 %d 个点格式错误（需要 x y 两列）", path, n + 1); fclose(f); return -1; }
    fclose(f);
    return n;
}

static int write_poly(const char *path, const PPoint *p, int n) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    for (int i = 0; i < n; ++i) fprintf(f, "%.6f %.6f\n", p[i].x, p[i].y);
    fclose(f);
    return 0;
}

static double read_double(const char *path, int *ok) {
    FILE *f = fopen(path, "r");
    *ok = 0;
    if (!f) return 0.0;
    double v;
    if (fscanf(f, "%lf", &v) == 1) *ok = 1;
    fclose(f);
    return v;
}

static int on_layer_cb(void *ctx, const PPoint *rem, int nrem,
                       const PPoint *hull, int nhull, const PLayer *info) {
    DRun *d = (DRun *)ctx;
    int li = info->layer - 1;
    char p[600];
    snprintf(p, sizeof p, "%s/layers/L%02d_rem.txt", d->outdir, info->layer);
    if (write_poly(p, rem, nrem) != 0) { snprintf(d->err, sizeof d->err, "写 %s 失败", p); return 9; }
    snprintf(p, sizeof p, "%s/layers/L%02d_hull.txt", d->outdir, info->layer);
    if (write_poly(p, hull, nhull) != 0) { snprintf(d->err, sizeof d->err, "写 %s 失败", p); return 9; }

    if (d->ref) {
        char gp[600], ga[600], gs[600], cmd[2400];
        snprintf(gp, sizeof gp, "%s/gold/L%02d_hull.txt", d->outdir, info->layer);
        snprintf(ga, sizeof ga, "%s/gold/L%02d_area.txt", d->outdir, info->layer);
        snprintf(gs, sizeof gs, "%s/gold/L%02d_gross.txt", d->outdir, info->layer);
        if (write_poly(gp, hull, nhull) != 0) { snprintf(d->err, sizeof d->err, "写 %.400s 失败", gp); return 9; }
        /* 主参照 trap：按顶点 y 切条的梯形积分，简单多边形上精确到舍入，
         * 用作 1e-4 门槛的判据。辅参照 area：仓库原有的 256x256 数格子，
         * 采样误差本身就有 1e-3 量级，只作旁证、不参与判据。 */
        snprintf(cmd, sizeof cmd, "'%s' trap '%s' > '%s' 2>/dev/null", d->ref, gp, ga);
        if (system(cmd) != 0) {
            snprintf(d->err, sizeof d->err, "参照 %.400s 在第 %d 层失败", d->ref, info->layer);
            return 9;
        }
        int ok = 0;
        d->gold[li] = read_double(ga, &ok);
        d->has_gold[li] = ok;
        if (!ok) { snprintf(d->err, sizeof d->err, "读 %.400s 失败", ga); return 9; }

        snprintf(cmd, sizeof cmd, "'%s' area '%s' > '%s' 2>/dev/null", d->ref, gp, gs);
        if (system(cmd) == 0) {
            int ok2 = 0;
            d->gross[li] = read_double(gs, &ok2);
            d->has_gross[li] = ok2;
            if (ok2 && d->gold[li] > 0.0) {
                double grel = fabs(d->gold[li] - d->gross[li]) / d->gold[li];
                if (grel > d->worst_gross_rel) {
                    d->worst_gross_rel = grel;
                    d->worst_gross_layer = info->layer;
                }
            }
        }
    }

    double rel = 0.0;
    if (d->has_gold[li] && d->gold[li] > 0.0) {
        double dev = fabs(info->area - d->gold[li]);
        rel = dev / d->gold[li];
        d->cum_rel += rel;
        if (rel > d->worst_rel) { d->worst_rel = rel; d->worst_layer = info->layer; }
        double tol = TOL_ABS + TOL_REL * fabs(d->gold[li]);
        if (dev > tol) {
            snprintf(d->err, sizeof d->err,
                     "第 %d 层面积与参照不一致：%.6f vs %.6f，相对偏差 %.3e 超过 %.0e",
                     info->layer, info->area, d->gold[li], rel, TOL_REL);
            d->failed = 1;
        }
    }

    if (d->rpt) {
        fprintf(d->rpt, "%d\t%d\t%d\t%d\t%.6f\t%.6f\t%.3e\t%.6f\t%.3e\t%.3f\n",
                info->layer, info->input_unique, info->hull_verts, info->removed,
                info->area,
                d->has_gold[li] ? d->gold[li] : 0.0,
                rel,
                d->has_gross[li] ? d->gross[li] : 0.0,
                (d->has_gross[li] && d->gold[li] > 0.0) ? fabs(d->gold[li] - d->gross[li]) / d->gold[li] : 0.0,
                info->ms);
        fflush(d->rpt);
    }
    return d->failed ? 8 : 0;
}

/* 跑一个点集：落盘、比对、反解、打印摘要。
 * 返回 0 通过；1 不一致；2 拒绝。 */
static int run_one(const char *tag, const PPoint *pts, int n, const char *outdir, const char *ref,
                   double *per_scenario_ms, int *layers_out, int *cover_out, double *ratio_out,
                   PSummary *sum_out, double *worst_rel_out) {
    static PLayer layers[PEEL_MAX_LAYERS];
    PSummary sum;
    PPeelOut out;
    memset(&out, 0, sizeof out);
    out.layers = layers;
    out.layer_cap = PEEL_MAX_LAYERS;

    DRun d;
    memset(&d, 0, sizeof d);
    d.outdir = outdir;
    d.ref = ref;
    d.worst_layer = -1;
    d.worst_gross_layer = -1;

    char mk[1400];
    snprintf(mk, sizeof mk, "mkdir -p '%s/layers' '%s/gold'", outdir, outdir);
    if (system(mk) != 0) { fprintf(stderr, "拒绝：无法创建输出目录 %s\n", outdir); return 2; }

    char rp[600];
    snprintf(rp, sizeof rp, "%s/report.tsv", outdir);
    d.rpt = fopen(rp, "w");
    if (!d.rpt) { fprintf(stderr, "拒绝：无法写报告 %s\n", rp); return 2; }
    fprintf(d.rpt, "layer\tin_unique\thull_verts\tremoved\tarea\tgold_trap\trel_dev\tgold_scan\tscan_rel\tms\n");
    out.on_layer = on_layer_cb;
    out.ctx = &d;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int rc = peel_run(pts, n, &out, &sum);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double total_ms = ((double)(t1.tv_sec - t0.tv_sec) * 1e9 +
                       (double)(t1.tv_nsec - t0.tv_nsec)) / 1e6;
    fclose(d.rpt);
    if (sum_out) *sum_out = sum; /* 拒绝路径也要能把层号/剩余点数带出去 */
    if (worst_rel_out) *worst_rel_out = d.worst_rel;

    if (rc == -1) { fprintf(stderr, "拒绝：%s 输入有误（点数或坐标非有限）\n", tag); return 2; }
    if (rc == -4) {
        fprintf(stderr, "拒绝：%s 层数超过上限 %d（第 %d 层）\n", tag, PEEL_MAX_LAYERS, sum.stop.layer);
        return 2;
    }
    if (rc == -5) {
        fprintf(stderr, "拒绝：%s 内部异常于第 %d 层（%s）\n", tag, sum.stop.layer,
                d.err[0] ? d.err : "未能移出任何点或面积回升");
        return 2;
    }
    if (rc == 9 || d.failed) {
        fprintf(stderr, "不一致：%s %s\n", tag, d.err);
        return 1;
    }
    if (rc == 3) {
        fprintf(stderr, "拒绝：%s 第 %d 层剩余点不足三点（实际 %d 个）\n",
                tag, sum.stop.layer, sum.stop.remaining);
        return 2;
    }
    if (rc == 2) {
        fprintf(stderr, "拒绝：%s 第 %d 层剩余点全部共线（实际 %d 个）\n",
                tag, sum.stop.layer, sum.stop.remaining);
        return 2;
    }
    if (rc != 0) {
        fprintf(stderr, "拒绝：%s 未知返回码 %d 于第 %d 层\n", tag, rc, sum.stop.layer);
        return 2;
    }

    /* 层面积单调不增：实现内部已核对，这里再从结果复核一遍。 */
    for (int i = 1; i < sum.layer_count; ++i) {
        if (layers[i].area > layers[i - 1].area * (1.0 + 1e-12)) {
            fprintf(stderr, "不一致：%s 第 %d 层面积回升\n", tag, i + 1);
            return 1;
        }
    }

    double ratio = 1.0;
    int k = peel_cover_layer(layers, sum.layer_count, COVER_FRAC, &ratio);

    double sum_layers_ms = 0.0;
    for (int i = 0; i < sum.layer_count; ++i) sum_layers_ms += layers[i].ms;

    if (per_scenario_ms) *per_scenario_ms = sum_layers_ms;
    if (layers_out) *layers_out = sum.layer_count;
    if (cover_out) *cover_out = k;
    if (ratio_out) *ratio_out = ratio;
    if (sum_out) *sum_out = sum;
    printf("== %s ==\n", tag);
    printf("点 %d（去重 %d），层数 %d，剥层计算 %.3f ms（各层之和），驱动总墙钟 %.3f ms\n",
           sum.input_points, sum.unique_points, sum.layer_count, sum_layers_ms, total_ms);
    printf("   层   进入   顶点   移出             面积           参照       相对差       ms\n");
    for (int i = 0; i < sum.layer_count; ++i) {
        const PLayer *L = &layers[i];
        if (d.has_gold[i])
            printf("  %3d %6d %6d %6d  %14.6f  %14.6f  %9.3e  %8.3f\n",
                   L->layer, L->input_unique, L->hull_verts, L->removed,
                   L->area, d.gold[i], fabs(L->area - d.gold[i]) / d.gold[i], L->ms);
        else
            printf("  %3d %6d %6d %6d  %14.6f  %14s  %9s  %8.3f\n",
                   L->layer, L->input_unique, L->hull_verts, L->removed,
                   L->area, "-", "-", L->ms);
    }
    if (ref) {
        printf("面积与参照最大相对差 %.3e（第 %d 层），容差 %.0e，%s\n",
               d.worst_rel, d.worst_layer > 0 ? d.worst_layer : 1, TOL_REL,
               d.worst_rel <= TOL_REL ? "通过" : "超标");
        printf("旁证：仓库原有 256x256 数格子参照与它的最大相对差 %.3e（第 %d 层）\n",
               d.worst_gross_rel, d.worst_gross_layer > 0 ? d.worst_gross_layer : 1);
    }
    if (k > 0) {
        printf("反解：剥到第 %d 层，剩余面积占比 %.4f%% <= 5%%\n", k, 100.0 * ratio);
    } else {
        printf("反解：无法覆盖 %.0f%%\n", 100.0 * COVER_FRAC);
    }
    printf("停止：%s（第 %d 层，该层剩余 %d 点）\n",
           sum.stop.reason == PEEL_STOP_DONE ? "剥完" :
           sum.stop.reason == PEEL_STOP_FEW ? "剩余不足三点" : "剩余全共线",
           sum.stop.layer, sum.stop.remaining);
    return 0;
}

/* ---------------- 场景点集 ---------------- */

static unsigned long long sm_state;
static void sm_seed(unsigned long long s) { sm_state = s; }
static unsigned long long sm_next(void) {
    sm_state += 0x9E3779B97F4A7C15ULL;
    unsigned long long z = sm_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
static double sm_unit(void) { return (double)(sm_next() >> 11) / 9007199254740992.0; }

/* 随机：200 个点，坐标保留三位小数，seed 固定可复现。 */
static int gen_random(PPoint *p, int cap, int n) {
    if (n > cap) return -1;
    sm_seed(42);
    for (int i = 0; i < n; ++i) {
        p[i].x = floor(sm_unit() * 1000000.0) / 1000.0;
        p[i].y = floor(sm_unit() * 1000000.0) / 1000.0;
    }
    return n;
}

/* 共线：64 个点全落在 y = 0.4x + 3 上。 */
static int gen_collinear(PPoint *p, int cap) {
    if (cap < 64) return -1;
    for (int i = 0; i < 64; ++i) {
        p[i].x = 5.0 * (double)i;
        p[i].y = 2.0 * (double)i + 3.0;
    }
    return 64;
}

/* 重复点：120 个基点，每个复制一份再打散，共 240 点、120 个不同位置。 */
static int gen_dups(PPoint *p, int cap, int nbase) {
    if (2 * nbase > cap) return -1;
    sm_seed(7);
    for (int i = 0; i < nbase; ++i) {
        p[i].x = floor(sm_unit() * 1000000.0) / 1000.0;
        p[i].y = floor(sm_unit() * 1000000.0) / 1000.0;
    }
    for (int i = 0; i < nbase; ++i) p[nbase + i] = p[i];
    int total = 2 * nbase;
    for (int i = total - 1; i > 0; --i) {
        int j = (int)(sm_next() % (unsigned long long)(i + 1));
        PPoint t = p[i]; p[i] = p[j]; p[j] = t;
    }
    return total;
}

/* 同心环：半径 100/80/60/40/20，每环 24 点；各环相位相同。 */
static int gen_rings(PPoint *p, int cap) {
    static const double radii[] = {100.0, 80.0, 60.0, 40.0, 20.0};
    const int per = 24;
    int need = (int)(sizeof radii / sizeof radii[0]) * per;
    if (need > cap) return -1;
    int k = 0;
    for (unsigned r = 0; r < sizeof radii / sizeof radii[0]; ++r)
        for (int i = 0; i < per; ++i) {
            double th = 2.0 * PI * (double)i / (double)per;
            p[k].x = 250.0 + radii[r] * cos(th);
            p[k].y = 250.0 + radii[r] * sin(th);
            ++k;
        }
    return k;
}

static int scenario(const char *name, const char *ref, FILE *sum,
                    double *ms_out, int *layers_out, int *k_out, double *ratio_out,
                    double *worst_out);

static int cmd_points(const char *name, const char *ref) {
    return scenario(name, ref, NULL, NULL, NULL, NULL, NULL, NULL);
}

/* 生成并跑一个场景，结果同时打到 stdout、可选写 summary 行。返回 run_one 的码。 */
static int scenario(const char *name, const char *ref, FILE *sum,
                    double *ms_out, int *layers_out, int *k_out, double *ratio_out,
                    double *worst_out) {
    static PPoint pts[PEEL_MAX_POINTS];
    int n;
    const char *sub;
    if (!strcmp(name, "random")) {
        n = gen_random(pts, PEEL_MAX_POINTS, 200);
        sub = "random";
    } else if (!strcmp(name, "collinear")) {
        n = gen_collinear(pts, PEEL_MAX_POINTS);
        sub = "collinear";
    } else if (!strcmp(name, "dup") || !strcmp(name, "duplicates")) {
        n = gen_dups(pts, PEEL_MAX_POINTS, 120);
        sub = "dup";
    } else if (!strcmp(name, "rings")) {
        n = gen_rings(pts, PEEL_MAX_POINTS);
        sub = "rings";
    } else {
        fprintf(stderr, "未知场景 %s（random|collinear|dup|rings）\n", name);
        return 2;
    }
    if (n < 0) { fprintf(stderr, "拒绝：生成 %s 失败\n", name); return 2; }

    char tag[160], outdir[300], f[500], mk[400];
    snprintf(tag, sizeof tag, "%s(%d 点)", name, n);
    snprintf(outdir, sizeof outdir, "out/%s", sub);
    snprintf(mk, sizeof mk, "mkdir -p '%s'", outdir);
    if (system(mk) != 0) { fprintf(stderr, "拒绝：无法创建 %s\n", outdir); return 2; }
    snprintf(f, sizeof f, "%s/points.txt", outdir);
    if (write_poly(f, pts, n) != 0) { fprintf(stderr, "拒绝：无法写 %s\n", f); return 2; }

    /* 先把去重后点数算出来给 summary 用（代价忽略不计） */
    PSummary sum_dummy;
    int layers_n = -1, k = -1;
    double ratio = -1.0, ms = -1.0;
    double worst = -1.0;
    int rc = run_one(tag, pts, n, outdir, ref, &ms, &layers_n, &k, &ratio, &sum_dummy, &worst);

    if (ms_out) *ms_out = ms;
    if (layers_out) *layers_out = layers_n;
    if (k_out) *k_out = k;
    if (ratio_out) *ratio_out = ratio;
    if (worst_out) *worst_out = worst;

    if (sum) {
        const char *status = rc == 0 ? "OK" : rc == 1 ? "MISMATCH" : "REJECTED";
        const char *reason =
            sum_dummy.stop.reason == PEEL_STOP_DONE ? "DONE" :
            sum_dummy.stop.reason == PEEL_STOP_FEW ? "FEW" :
            sum_dummy.stop.reason == PEEL_STOP_COLLINEAR ? "COLLINEAR" :
            sum_dummy.stop.reason == PEEL_STOP_CAPACITY ? "CAPACITY" : "INTERNAL";
        if (rc == 0)
            fprintf(sum, "%s\t%d\t%d\t%d\t%d\t%.6f\t%.3f\t%.3e\t%d\t%s\t%s\n",
                    name, sum_dummy.input_points, sum_dummy.unique_points,
                    sum_dummy.layer_count, k, ratio, ms, worst, sum_dummy.stop.layer, reason, status);
        else
            fprintf(sum, "%s\t%d\t%d\t%d\t-\t-\t-\t-\t%d\t%s\t%s\n",
                    name, sum_dummy.input_points, sum_dummy.unique_points,
                    sum_dummy.layer_count, sum_dummy.stop.layer, reason, status);
        fflush(sum);
    }
    return rc;
}

/* 四个场景一次跑完，汇总写到 out/summary.tsv。返回失败场景数。 */
static int cmd_all(const char *ref) {
    static const char *names[] = {"random", "collinear", "dup", "rings"};
    char mk[200];
    snprintf(mk, sizeof mk, "mkdir -p out");
    if (system(mk) != 0) { fprintf(stderr, "拒绝：无法创建 out\n"); return 2; }
    FILE *sum = fopen("out/summary.tsv", "w");
    if (!sum) { fprintf(stderr, "拒绝：无法写 out/summary.tsv\n"); return 2; }
    fprintf(sum, "scenario\tpoints\tunique\tlayers\tcover_layer\trest_ratio\tpeel_ms\tworst_rel_dev\tstop_layer\tstop_reason\tstatus\n");

    int bad = 0;
    for (unsigned i = 0; i < sizeof names / sizeof names[0]; ++i) {
        double ms = -1.0;
        int nlayers = -1, k = -1;
        double ratio = -1.0;
        double worst = -1.0;
        int rc = scenario(names[i], ref, sum, &ms, &nlayers, &k, &ratio, &worst);
        /* 共线场景本来就应当被拒（rc=2）；其余场景必须通过 */
        int expected = strcmp(names[i], "collinear") == 0 ? 2 : 0;
        if (rc != expected) ++bad;
    }
    fclose(sum);
    printf("\n汇总写到 out/summary.tsv（%d 个场景失败）\n", bad);
    return bad ? 1 : 0;
}

static int cmd_cover(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "用法: run cover <area1> <area2> ... <frac>\n");
        return 2;
    }
    double frac = atof(argv[argc - 1]);
    int n = argc - 2;
    if (n > PEEL_MAX_LAYERS) { fprintf(stderr, "拒绝：层数超过 %d\n", PEEL_MAX_LAYERS); return 2; }
    static PLayer layers[PEEL_MAX_LAYERS];
    for (int i = 0; i < n; ++i) {
        memset(&layers[i], 0, sizeof layers[i]);
        layers[i].layer = i + 1;
        layers[i].area = atof(argv[i + 1]);
        if (!(layers[i].area >= 0.0)) { fprintf(stderr, "拒绝：第 %d 层面积非法\n", i + 1); return 2; }
        if (i > 0 && layers[i].area > layers[i - 1].area) {
            fprintf(stderr, "拒绝：第 %d 层面积比上一层大\n", i + 1);
            return 2;
        }
    }
    double ratio = 1.0;
    int k = peel_cover_layer(layers, n, frac, &ratio);
    if (k < 0) { fprintf(stderr, "拒绝：层面积数据无效\n"); return 2; }
    printf("覆盖 %.4f 需剥到第 %d 层，剩余占比 %.6f\n", frac, k, ratio);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "用法: run one <pts.txt> <outdir> [ref]\n"
                "      run points <name> [ref]\n"
                "      run all [ref]\n"
                "      run cover <a1> <a2> ... <frac>\n");
        return 2;
    }
    const char *ref = getenv("REF");
    if (!strcmp(argv[1], "one")) {
        if (argc < 4) { fprintf(stderr, "用法: run one <pts.txt> <outdir> [ref]\n"); return 2; }
        static PPoint pts[PEEL_MAX_POINTS];
        char err[512] = {0};
        int n = load_pts(argv[2], pts, PEEL_MAX_POINTS, err, sizeof err);
        if (n < 0) { fprintf(stderr, "拒绝：%s\n", err); return 2; }
        return run_one(argv[2], pts, n, argv[3], argc >= 5 ? argv[4] : ref, NULL, NULL, NULL, NULL, NULL, NULL);
    }
    if (!strcmp(argv[1], "points")) {
        if (argc < 3) { fprintf(stderr, "用法: run points <name> [ref]\n"); return 2; }
        return cmd_points(argv[2], argc >= 4 ? argv[3] : ref);
    }
    if (!strcmp(argv[1], "all")) return cmd_all(argc >= 3 ? argv[2] : ref);
    if (!strcmp(argv[1], "cover")) return cmd_cover(argc, argv);
    fprintf(stderr, "未知子命令 %s\n", argv[1]);
    return 2;
}
