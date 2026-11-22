/* 缩放层检查与报告工具。
 *
 * 用法：check           打印四档缩放报告、整数层报告、跳变对比
 *       check --verbose 额外打印每档两档缩放
 *
 * 退出码：0 全部断言通过；1 有断言失败。
 */
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/metric.h"
#include "src/zoom.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 64
#define H 64
#define SS 4
#define SWEEP 16 /* 每个倍程内的采样数 */
#define RUNS 5

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int build_scene(RScene *s) {
    RPoint sq[4] = {{-1.0, -1.0}, {1.0, -1.0}, {1.0, 1.0}, {-1.0, 1.0}};
    RPoint tri[3] = {{-0.6, -0.2}, {0.4, -0.5}, {0.1, 0.55}};
    scene_init(s);
    if (scene_add_poly(s, sq, 4) != 0) return -1;
    if (scene_add_poly(s, tri, 3) != 0) return -2;
    return 0;
}

/* 与 cli 一致：位置固定、不旋转，缩放为 place.scale。 */
static int render_scale(const RScene *s, RBox win, double scale, const RZoomOpts *o,
                        unsigned char *buf, RZoomReport *rep) {
    RPlace place = {0.0, 0.0, 0.0, scale};
    return pipeline_render_zoom(s, place, win, W, H, o, buf, rep);
}

static int cmp_ulong(const void *a, const void *b) {
    unsigned long x = *(const unsigned long*)a, y = *(const unsigned long*)b;
    return (x > y) - (x < y);
}

/* 窗口四周留出余量，让 1/4x..4x 的内容都还在画面里。 */
static RBox padded_box(RBox b, double pad) {
    double cx = (b.minx + b.maxx) * 0.5, cy = (b.miny + b.maxy) * 0.5;
    double hw = (b.maxx - b.minx) * 0.5 * pad, hh = (b.maxy - b.miny) * 0.5 * pad;
    RBox r = {cx - hw, cy - hh, cx + hw, cy + hh};
    return r;
}

/* 单个倍程内的最大与平均跳变度。lo 为下档整数层。 */
static void sweep_octave(const RScene *s, RBox win, int lo, int blend,
                         double *out_max, double *out_mean) {
    static unsigned char frames[SWEEP + 1][W * H];
    const unsigned char *ptrs[SWEEP + 1];
    RZoomOpts o = {-1, SS, blend};
    for (int k = 0; k <= SWEEP; ++k) {
        double scale = exp2((double)lo + (double)k / SWEEP);
        int rc = render_scale(s, win, scale, &o, frames[k], NULL);
        if (rc != 0) { printf("sweep render failed rc=%d\n", rc); ++fails; }
        ptrs[k] = frames[k];
    }
    *out_max = metric_sequence_max_jump(ptrs, W * H, SWEEP + 1);
    *out_mean = metric_sequence_mean_jump(ptrs, W * H, SWEEP + 1);
}

int main(int argc, char **argv) {
    int verbose = 0;
    for (int i = 1; i < argc; ++i) if (!strcmp(argv[i], "--verbose")) verbose = 1;

    RScene s;
    if (build_scene(&s) != 0) { printf("scene build failed\n"); return 1; }
    RBox win;
    if (scene_bbox(&s, &win) != 0) { printf("bbox failed\n"); return 1; }
    RBox big = padded_box(win, 8.0);

    unsigned char buf[W * H];
    RZoomOpts on = {-1, SS, 1};

    /* 四档缩放：0.6、1.0、1.7、3.3，其中 1.0 恰在整数层。
     * 旧路径不会留下画布边缘的覆盖率，逐字节回归另见 make golden。 */
    const double scales[4] = {0.6, 1.0, 1.7, 3.3};
    static RZoomReport reps[4];
    static unsigned long times[4][RUNS];
    static double step_jump[4];
    static unsigned char nb[W * H];
    for (int k = 0; k < 4; ++k) {
        for (int r = 0; r < RUNS; ++r) {
            int rc = render_scale(&s, big, scales[k], &on, buf, &reps[k]);
            CK(rc == 0);
            times[k][r] = reps[k].us;
        }
        qsort(times[k], RUNS, sizeof(unsigned long), cmp_ulong);
        /* 与 1/16 倍程处（同一网格步长）的画面比较，得到该档的局部跳变。 */
        CK(render_scale(&s, big, scales[k] * exp2(1.0 / 16.0), &on, nb, NULL) == 0);
        step_jump[k] = metric_jump(buf, nb, W * H);
    }
    printf("== 四档缩放 (canvas %dx%d, ss=%d, 每档 %d 次取中位) ==\n", W, H, SS, RUNS);
    printf("%-8s %-10s %-4s %-4s %-7s %-5s %-10s %s\n",
           "scale", "level", "lo", "hi", "w", "exact", "us(median)", "step_jump");
    for (int k = 0; k < 4; ++k) {
        printf("%-8.4f %-10.6f %-4d %-4d %-7.4f %-5s %-10lu %.4f",
               scales[k], reps[k].level, reps[k].lo, reps[k].hi, reps[k].w,
               reps[k].exact ? "yes" : "no", times[k][RUNS / 2], step_jump[k]);
        if (verbose) printf("  (2^%d=%.6f, 2^%d=%.6f, effective=%.6f)",
                            reps[k].lo, reps[k].scale_lo, reps[k].hi,
                            reps[k].scale_hi, reps[k].scale_eff);
        printf("\n");
        /* 层号必须与 log2(scale) 一致，权重就是小数部分。 */
        CK(fabs(reps[k].level - log2(scales[k])) < 1e-12);
        CK(reps[k].hi == reps[k].lo + 1);
        CK(reps[k].lo == (int)floor(log2(scales[k]) + 1e-12));
        CK(fabs(reps[k].w - (log2(scales[k]) - reps[k].lo)) < 1e-9);
        CK(fabs(reps[k].scale_eff - scales[k]) < 1e-12);
    }

    /* 恰好落在整数层：单独报，权重为 0，只取一档。 */
    printf("\n== 恰在整数层 (单独判定) ==\n");
    const double ints[] = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    for (size_t k = 0; k < sizeof(ints) / sizeof(ints[0]); ++k) {
        RZoomReport r;
        CK(render_scale(&s, win, ints[k], &on, buf, &r) == 0);
        printf("scale %-6.4f level %-4.0f lo %-3d hi %-3d w %.1f exact %s\n",
               ints[k], r.level, r.lo, r.hi, r.w, r.exact ? "yes" : "no");
        CK(r.exact);
        CK(r.w == 0.0);
        CK(r.lo == (int)llround(log2(ints[k])));
        CK(r.hi == r.lo + 1);
        /* 恰在整数层时不应做两档插值：结果必须与单独渲染该档完全相同。 */
        RZoomOpts forced = {r.lo, SS, 1};
        unsigned char direct[W * H];
        RPlace place = {0.0, 0.0, 0.0, ints[k]};
        CK(pipeline_render_zoom(&s, place, win, W, H, &forced, direct, NULL) == 0);
        CK(memcmp(buf, direct, W * H) == 0);
    }

    /* 缩放为零或负：拒绝并说明。 */
    printf("\n== 非法缩放 ==\n");
    struct { double scale; int expect; const char *why; } bad[] = {
        {0.0,  -2, "缩放为零，log2 无定义"},
        {-1.0, -2, "缩放为负，取对数无意义"},
        {-0.0, -2, "负零同样是零"},
        {NAN,  -7, "NaN 不是有限数"},
        {INFINITY, -7, "无穷大不是有限缩放"},
    };
    for (size_t k = 0; k < sizeof(bad) / sizeof(bad[0]); ++k) {
        RZoomReport r;
        int rc = render_scale(&s, win, bad[k].scale, &on, buf, &r);
        printf("scale %-8g -> rc %d (%s)\n", bad[k].scale, rc, bad[k].why);
        CK(rc == bad[k].expect);
    }
    RZoomLevel zl;
    CK(zoom_level_from_scale(0.0, &zl) == -2);
    CK(zoom_level_from_scale(-2.0, &zl) == -2);
    CK(zoom_level_from_scale(NAN, &zl) == -3);
    CK(zoom_level_from_scale(INFINITY, &zl) == -3);

    /* 关掉新逻辑：整数层输出必须与旧整数层路径逐字节相同。 */
    printf("\n== 关掉新逻辑时整数层逐字节一致 ==\n");
    RZoomOpts off = {-1, SS, 0};
    unsigned char legacy[W * H], cut[W * H];
    for (size_t k = 0; k < sizeof(ints) / sizeof(ints[0]); ++k) {
        RPlace place = {0.0, 0.0, 0.0, ints[k]};
        CK(zoom_level_from_scale(ints[k], &zl) == 0 && zl.exact);
        CK(pipeline_render(&s, place, win, W, H, legacy) == 0);
        CK(pipeline_render_zoom(&s, place, win, W, H, &off, cut, NULL) == 0);
        int same = memcmp(legacy, cut, W * H) == 0;
        printf("scale %-6.4f old==new(blend off): %s\n", ints[k], same ? "IDENTICAL" : "DIFF");
        CK(same);
    }

    /* 跳变对比：同一倍程里逐级缩放，混合的最大跳变必须小于硬切的一半。 */
    printf("\n== 跳变对比 (每个倍程 %d 步采样) ==\n", SWEEP);
    printf("%-6s %-10s %-10s %-10s %-10s %-8s\n",
           "octave", "blend_max", "cut_max", "blend_mean", "cut_mean", "ratio");
    double worst_ratio = 0.0, sum_ratio = 0.0;
    int noct = 0;
    for (int lo = -2; lo <= 2; ++lo) {
        double bmax, bmean, cmax, cmean;
        sweep_octave(&s, big, lo, 1, &bmax, &bmean);
        sweep_octave(&s, big, lo, 0, &cmax, &cmean);
        double ratio = cmax > 0.0 ? bmax / cmax : 0.0;
        if (ratio > worst_ratio) worst_ratio = ratio;
        sum_ratio += ratio;
        ++noct;
        printf("2^%-4d %-10.4f %-10.4f %-10.4f %-10.4f %.4f\n",
               lo, bmax, cmax, bmean, cmean, ratio);
        CK(cmax > 0.0);
        CK(bmax <= cmax * 0.5); /* 关键约束：跳变小于硬切的一半 */
        CK(bmean <= cmean + 1e-12);
    }
    printf("worst blend/cut max-jump ratio: %.4f (mean %.4f), 要求 <= 0.5\n",
           worst_ratio, sum_ratio / noct);
    CK(worst_ratio <= 0.5);

    printf("\n%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
