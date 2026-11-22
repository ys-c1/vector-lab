#include "scene.h"
#include "pipeline.h"
#include "raster.h"
#include "export.h"
#include "winplan.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int feq(double a, double b, double tol) { return fabs(a - b) <= tol; }

/* 铺满整幅图的掩码：所有像素都是图形。 */
static unsigned char *full_mask(int w, int h) {
    unsigned char *m = (unsigned char *)malloc((size_t)w * h);
    memset(m, 1, (size_t)w * h);
    return m;
}

static RScene square_scene(double s) {
    RPoint sq[4] = {{0, 0}, {s, 0}, {s, s}, {0, s}};
    RScene sc;
    scene_init(&sc);
    scene_add_poly(&sc, sq, 4);
    return sc;
}

static void test_defaults(void) {
    RWinPlanOpt o;
    CK(winplan_default_opt(&o) == WINPLAN_OK);
    CK(o.win_w == 40 && o.win_h == 40 && o.stride == 8);
    CK(feq(o.target, 0.98, 1e-15));
    CK(feq(o.overlap_cap, 0.60, 1e-15));
    CK(o.max_wins == WINPLAN_MAX_WINS && o.fixed_count == 0);
    CK(winplan_default_opt(NULL) == WINPLAN_ERR_ARG);

    /* 候选计数：64 宽、32 窗、步长 8 -> 0,8,16,24,32（32 正好贴边，不重复）。 */
    RWinPlanOpt c = {32, 32, 8, 0.98, 0.6, 256, 0};
    CK(winplan_candidate_count(64, 64, &c) == 25);
    /* 65 宽 -> 0,8,16,24,32 再补贴边 33，共 6 个。 */
    CK(winplan_candidate_count(65, 65, &c) == 36);
    /* 窗口比画面大：拒绝。 */
    CK(winplan_candidate_count(16, 16, &c) == -1);
    /* 参数越界。 */
    RWinPlanOpt bad = c;
    bad.stride = 0;
    CK(winplan_candidate_count(64, 64, &bad) == -1);
    bad = c;
    bad.target = 0.0;
    CK(winplan_candidate_count(64, 64, &bad) == -1);
    bad = c;
    bad.overlap_cap = 1.0;
    CK(winplan_candidate_count(64, 64, &bad) == -1);
}

static void test_plan_guards(void) {
    RWinPlan plan;
    RWinPlanOpt o = {8, 8, 8, 0.98, 0.5, 256, 0};
    unsigned char m[16 * 16];
    memset(m, 1, sizeof m);
    CK(winplan_plan(NULL, 16, 16, &o, &plan) == WINPLAN_ERR_ARG);
    CK(winplan_plan(m, 16, 16, NULL, &plan) == WINPLAN_ERR_ARG);
    CK(winplan_plan(m, 16, 16, &o, NULL) == WINPLAN_ERR_ARG);
    RWinPlanOpt big = o;
    big.win_w = 32;
    CK(winplan_plan(m, 16, 16, &big, &plan) == WINPLAN_ERR_ARG);
    RWinPlanOpt bad = o;
    bad.max_wins = 0;
    CK(winplan_plan(m, 16, 16, &bad, &plan) == WINPLAN_ERR_ARG);
}

static void test_basic_greedy(void) {
    /* 64x64，全图都是图形，窗口 32x32 步长 8：贪心必须 4 个窗口全覆盖，
     * 且互相不重叠（对齐到四角）。 */
    enum { W = 64, H = 64 };
    RWinPlanOpt o = {32, 32, 8, 0.98, 0.6, 256, 0};
    unsigned char *m = full_mask(W, H);
    RWinPlan p;
    CK(winplan_plan(m, W, H, &o, &p) == WINPLAN_OK);
    CK(p.stop == WINPLAN_STOP_FULL);
    CK(p.nwins == 4);
    CK(p.on_pixels == W * H && p.covered == W * H);
    CK(feq(p.coverage, 1.0, 1e-15));
    CK(feq(p.overlap, 0.0, 1e-15));
    CK(p.sum_area == 4LL * 32 * 32 && p.union_area == W * H);
    /* 四个窗口必须两两不重叠，并且合起来正好铺满。 */
    int cover[W * H];
    memset(cover, 0, sizeof cover);
    for (int k = 0; k < p.nwins; ++k) {
        CK(p.wins[k].w == 32 && p.wins[k].h == 32);
        for (int y = p.wins[k].y; y < p.wins[k].y + 32; ++y)
            for (int x = p.wins[k].x; x < p.wins[k].x + 32; ++x) ++cover[y * W + x];
    }
    int over = 0, miss = 0;
    for (int i = 0; i < W * H; ++i) {
        if (cover[i] > 1) ++over;
        if (cover[i] == 0) ++miss;
    }
    CK(over == 0 && miss == 0);
    free(m);
}

static void test_full_beats_target(void) {
    /* 目标 0.9，但贪心窗口正好一次铺满：必须报 FULL 而不是 TARGET，
     * 覆盖率 1.0，窗口数不因目标提前收缩。 */
    enum { W = 32, H = 32 };
    RWinPlanOpt o = {32, 32, 8, 0.90, 0.6, 256, 0};
    unsigned char *m = full_mask(W, H);
    RWinPlan p;
    CK(winplan_plan(m, W, H, &o, &p) == WINPLAN_OK);
    CK(p.stop == WINPLAN_STOP_FULL);
    CK(p.nwins == 1);
    CK(feq(p.coverage, 1.0, 1e-15));
    free(m);
}

static void test_target_stop(void) {
    /* 一行 64x8 的图形条带，窗口 8x8 步长 8：每个窗口覆盖 8x8=64 个像素，
     * 目标 0.75 时应在第 6 个窗口（384/512=0.75）停下。 */
    enum { W = 64, H = 16 };
    RWinPlanOpt o = {8, 8, 8, 0.75, 0.9, 256, 0};
    unsigned char *m = (unsigned char *)calloc(W * H, 1);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 64; ++x) m[y * W + x] = 1;
    RWinPlan p;
    CK(winplan_plan(m, W, H, &o, &p) == WINPLAN_OK);
    CK(p.stop == WINPLAN_STOP_TARGET);
    CK(p.covered == 384 && p.on_pixels == 512);
    CK(feq(p.coverage, 0.75, 1e-15));
    CK(p.nwins == 6);
    free(m);
}

static void test_overlap_cap_rejects(void) {
    /* 全图图形、窗口 48x48：贪心会先放 (0,0)，再放对角 (16,16)（两者重叠
     * 32/48 宽、32/48 高）。再往下任何一块都要与这两块重叠超过 0.30 的上限
     * 或增益更小，全部被拒 -> OVERLAP，已接受集合仍满足上限。 */
    enum { W = 64, H = 64 };
    RWinPlanOpt o = {48, 48, 8, 0.98, 0.30, 256, 0};
    unsigned char *m = full_mask(W, H);
    RWinPlan p;
    int rc = winplan_plan(m, W, H, &o, &p);
    CK(rc == WINPLAN_ERR_OVERLAP);
    CK(p.stop == WINPLAN_STOP_OVERLAP);
    CK(p.nrejects > 0);
    CK(p.overlap <= 0.30 + 1e-12);
    CK(p.coverage < 0.98);
    /* 每条拒绝记录都要真的超过上限。 */
    for (int k = 0; k < p.nrejects && k < WINPLAN_MAX_REJECTS; ++k)
        CK(p.rejects[k].overlap > 0.30 + 1e-12);
    /* nwins 计数的窗口两两并集/面积和应当就是报出的重叠率。 */
    CK(p.sum_area == (long long)p.nwins * 48 * 48);
    free(m);
}

static void test_zero_overlap_cap(void) {
    /* 上限 0（硬约束）：20x1 的图形条带，窗口 16x16 只有候选 (0,0) 与 (8,0)。
     * 接受 (0,0) 后再放 (8,0) 会重叠 8x16，上限为零 -> 拒绝并停 OVERLAP；
     * 已接受集合的重叠率必须仍为 0。 */
    enum { W = 24, H = 16 };
    RWinPlanOpt o = {16, 16, 8, 1.0, 0.0, 256, 0};
    unsigned char *m = (unsigned char *)calloc(W * H, 1);
    for (int x = 0; x < 20; ++x) m[x] = 1;
    RWinPlan p;
    CK(winplan_plan(m, W, H, &o, &p) == WINPLAN_ERR_OVERLAP);
    CK(p.stop == WINPLAN_STOP_OVERLAP);
    CK(feq(p.overlap, 0.0, 1e-15));
    CK(p.nwins == 1 && p.nrejects == 1);
    CK(p.rejects[0].x == 8 && p.rejects[0].y == 0);
    CK(fabs(p.rejects[0].overlap - 0.25) < 1e-12);
    CK(fabs(p.coverage - 0.8) < 1e-12);
    free(m);
}

static void test_no_gain(void) {
    /* 图形像素在 (9,9)，而候选格点是 0/16/32：任何 8x8 候选窗口都盖不到它，
     * 所有增益为 0 -> NOGAIN + 目标未达（rc=-2），0 个窗口。 */
    enum { W = 32, H = 32 };
    RWinPlanOpt o = {8, 8, 16, 0.5, 0.6, 256, 0};
    unsigned char *m = (unsigned char *)calloc(W * H, 1);
    m[9 * W + 9] = 1;
    RWinPlan p;
    int rc = winplan_plan(m, W, H, &o, &p);
    CK(rc == WINPLAN_ERR_TARGET);
    CK(p.stop == WINPLAN_STOP_NOGAIN);
    CK(p.nwins == 0);
    free(m);
}

static void test_empty_scene(void) {
    /* 空场景：0 个窗口，覆盖率约定为 1，停 EMPTY。 */
    RWinPlanOpt o = {8, 8, 8, 0.98, 0.6, 256, 0};
    unsigned char m[16 * 16];
    memset(m, 0, sizeof m);
    RWinPlan p;
    CK(winplan_plan(m, 16, 16, &o, &p) == WINPLAN_OK);
    CK(p.stop == WINPLAN_STOP_EMPTY);
    CK(p.nwins == 0 && p.on_pixels == 0);
    CK(feq(p.coverage, 1.0, 1e-15));
}

static void test_maxwin(void) {
    /* 上限 2 个窗口：全图铺满时覆盖率无从达到 0.98 -> MAXWIN，rc=-2。 */
    enum { W = 64, H = 64 };
    RWinPlanOpt o = {16, 16, 8, 0.98, 0.9, 2, 0};
    unsigned char *m = full_mask(W, H);
    RWinPlan p;
    CK(winplan_plan(m, W, H, &o, &p) == WINPLAN_ERR_TARGET);
    CK(p.stop == WINPLAN_STOP_MAXWIN);
    CK(p.nwins == 2);
    free(m);
}

static void test_fixed_compare(void) {
    /* 固定 4 个窗口（不做贪心、不查重叠上限）对照贪心：图形是左上一个
     * 12x12 小方块，扫描序前 4 个候选窗口全在左上角彼此重叠、也几乎盖不到
     * 其余区域；贪心则先挑盖满小方块的那块，1 个窗口就达到全覆盖。 */
    enum { W = 64, H = 64 };
    RWinPlanOpt g = {32, 32, 8, 0.90, 0.6, 256, 0};
    RWinPlanOpt f = g;
    f.fixed_count = 4;
    unsigned char *m = (unsigned char *)calloc(W * H, 1);
    for (int y = 30; y < 40; ++y)
        for (int x = 30; x < 40; ++x) m[y * W + x] = 1;
    RWinPlan gp, fp;
    CK(winplan_plan(m, W, H, &g, &gp) == WINPLAN_OK);
    CK(winplan_plan(m, W, H, &f, &fp) == WINPLAN_OK);
    CK(gp.stop == WINPLAN_STOP_FULL && gp.nwins == 1);
    CK(feq(gp.coverage, 1.0, 1e-15));
    CK(fp.stop == WINPLAN_STOP_FIXED && fp.nwins == 4);
    CK(fp.coverage < gp.coverage);
    CK(fp.overlap > gp.overlap);
    /* 固定模式的窗口顺序就是扫描序：y 升序、同 y 内 x 升序。 */
    for (int k = 1; k < fp.nwins; ++k) {
        const RWin *a = &fp.wins[k - 1], *b = &fp.wins[k];
        CK(a->y < b->y || (a->y == b->y && a->x < b->x));
    }
    free(m);
}

static void test_render_region_identity(void) {
    /* raster_fill_region 必须与整幅 raster_fill 逐字节一致（任意子矩形）。 */
    RScene sc = square_scene(10.0);
    RBox b;
    CK(scene_bbox(&sc, &b) == 0);
    enum { W = 32, H = 32 };
    unsigned char full[W * H], part[W * H];
    CK(raster_fill(&sc, &b, W, H, 4, full) == 0);
    memset(part, 7, sizeof part);
    CK(raster_fill_region(&sc, &b, W, H, 4, 5, 7, 11, 13, part) == 0);
    int same = 1, outside_untouched = 1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int inside = x >= 5 && x < 16 && y >= 7 && y < 20;
            if (inside) { if (part[y * W + x] != full[y * W + x]) same = 0; }
            else if (part[y * W + x] != 7) outside_untouched = 0;
        }
    CK(same && outside_untouched);
    /* 越界窗口拒绝。 */
    CK(raster_fill_region(&sc, &b, W, H, 4, 30, 0, 5, 5, part) == -1);
    CK(raster_fill_region(&sc, &b, W, H, 4, 0, 0, 0, 5, part) == -1);
}

static void test_render_full_identity(void) {
    /* 计划停 FULL 时，winplan_render 必须与整幅 pipeline_render 逐字节相同。 */
    RScene sc = square_scene(10.0);
    RBox b;
    CK(scene_bbox(&sc, &b) == 0);
    enum { W = 48, H = 48 };
    unsigned char full[W * H], part[W * H];
    RPlace place = {0, 0, 0, 1.0};
    CK(pipeline_render(&sc, place, b, W, H, full) == 0);
    RWinPlanOpt o = {24, 24, 8, 0.98, 0.6, 256, 0};
    RWinPlan p;
    CK(winplan_plan(full, W, H, &o, &p) == WINPLAN_OK);
    CK(p.stop == WINPLAN_STOP_FULL);
    CK(winplan_render(&sc, place, b, &p, W, H, part) == 0);
    CK(memcmp(full, part, sizeof full) == 0);
    /* 参数守卫。 */
    CK(winplan_render(NULL, place, b, &p, W, H, part) == WINPLAN_ERR_ARG);
    RBox bad = {0, 0, 0, 0};
    CK(winplan_render(&sc, place, bad, &p, W, H, part) == -3);
    RPlace zero = {0, 0, 0, 0.0};
    CK(winplan_render(&sc, zero, b, &p, W, H, part) == -2);
}

static void test_partial_render_matches_plan(void) {
    /* 非全覆盖计划：渲染点亮的像素数 = 计划 covered，且都是图形像素。 */
    RScene sc = square_scene(10.0);
    RBox b;
    CK(scene_bbox(&sc, &b) == 0);
    enum { W = 40, H = 40 };
    unsigned char full[W * H], part[W * H];
    RPlace place = {0, 0, 0, 1.0};
    CK(pipeline_render(&sc, place, b, W, H, full) == 0);
    /* 目标 0.5，窗口 8x8：只画一半左右。 */
    RWinPlanOpt o = {8, 8, 8, 0.5, 0.6, 256, 0};
    RWinPlan p;
    CK(winplan_plan(full, W, H, &o, &p) == WINPLAN_OK);
    CK(p.stop == WINPLAN_STOP_TARGET);
    CK(winplan_render(&sc, place, b, &p, W, H, part) == 0);
    CK(export_count_on(part, W, H) == p.covered);
    int subset = 1;
    for (int i = 0; i < W * H; ++i)
        if (part[i] && !full[i]) subset = 0;
    CK(subset);
    CK(p.covered < p.on_pixels);
}

static void test_str(void) {
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_FULL), "FULL") == 0);
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_TARGET), "TARGET") == 0);
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_OVERLAP), "OVERLAP") == 0);
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_NOGAIN), "NOGAIN") == 0);
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_MAXWIN), "MAXWIN") == 0);
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_FIXED), "FIXED") == 0);
    CK(strcmp(winplan_stop_str(WINPLAN_STOP_EMPTY), "EMPTY") == 0);
    CK(strcmp(winplan_stop_str(-99), "?") == 0);
}

int main(void) {
    test_defaults();
    test_plan_guards();
    test_basic_greedy();
    test_full_beats_target();
    test_target_stop();
    test_overlap_cap_rejects();
    test_zero_overlap_cap();
    test_no_gain();
    test_empty_scene();
    test_maxwin();
    test_fixed_compare();
    test_render_region_identity();
    test_render_full_identity();
    test_partial_render_matches_plan();
    test_str();
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
