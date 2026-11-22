#include "atlas.h"
#include <stdio.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 校验放置：全部在边长内、互不重叠、尺寸不变。 */
static int layout_valid(const Tiles *t, int side, const int *b, int n) {
    for (int i = 0; i < n; ++i) {
        int x = b[4*i+0], y = b[4*i+1], w = b[4*i+2], h = b[4*i+3];
        if (w != t->t[i].w || h != t->t[i].h) return 0;
        if (x < 0 || y < 0 || x + w > side || y + h > side) return 0;
    }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            int ax = b[4*i+0], ay = b[4*i+1], aw = b[4*i+2], ah = b[4*i+3];
            int bx = b[4*j+0], by = b[4*j+1], bw = b[4*j+2], bh = b[4*j+3];
            int sep = ax + aw <= bx || bx + bw <= ax || ay + ah <= by || by + bh <= ay;
            if (!sep) return 0;
        }
    return 1;
}

static int is_pow2(int v) { return v > 0 && (v & (v - 1)) == 0; }

static void check_samples(double limit) {
    Tiles t;
    AtlasResult r;
    static int boxes[4 * ATLAS_MAX_TILES];
    int codes[4];
    const char *names[4] = {"icons", "mixed", "strips", "uniform"};

    for (int s = 0; s < 4; ++s) {
        if (s == 0) atlas_sample_icons(&t);
        else if (s == 1) atlas_sample_mixed(&t);
        else if (s == 2) atlas_sample_strips(&t);
        else atlas_sample_uniform(&t);
        codes[s] = atlas_solve(&t, limit, &r);
        CK(codes[s] == ATLAS_OK || codes[s] == ATLAS_EWASTE);
        CK(r.packed == 1);
        CK(r.boxes == t.n);
        CK(is_pow2(r.side));
        CK(r.side >= r.side_raw);
        CK(!r.pow2_rounded || r.side == r.side_raw * 2 || r.side > r.side_raw);

        int ok = atlas_pack(&t, r.side, boxes, ATLAS_MAX_TILES);
        CK(ok == 1);
        CK(layout_valid(&t, r.side, boxes, t.n));
        /* 最小性：side_raw-1 确实不可行（放不进即返回 0 或 ETOOBIG）。 */
        if (r.side_raw > 1) CK(atlas_pack(&t, r.side_raw - 1, NULL, 0) <= 0);
        printf("%-8s solve=%d side=%d raw=%d %s waste=%.4f limit=%.2f %s\n",
               names[s], codes[s], r.side, r.side_raw,
               r.pow2_rounded ? "rounded-up" : "exact",
               r.waste, r.limit, r.waste <= r.limit ? "PASS" : "FAIL");
        (void)codes;
    }
}

int main(void) {
    Tiles t;

    /* 空图集拒绝。 */
    t.n = 0;
    AtlasResult r;
    CK(atlas_solve(&t, 0.5, &r) == ATLAS_EINPUT);
    printf("empty atlas -> EINPUT\n");

    /* 图块非正尺寸拒绝。 */
    t.n = 1; t.t[0].w = 0; t.t[0].h = 8;
    CK(atlas_solve(&t, 0.5, &r) == ATLAS_EINPUT);
    t.t[0].w = 8; t.t[0].h = -1;
    CK(atlas_solve(&t, 0.5, &r) == ATLAS_EINPUT);
    printf("zero/negative tile -> EINPUT\n");

    /* 图块大于边长：单块 100x10，没有尺寸上限时仍可解；
     * 用极小上界直接请求装箱，必须报告放不进。 */
    t.n = 1; t.t[0].w = 100; t.t[0].h = 10;
    CK(atlas_pack(&t, 64, NULL, 0) == ATLAS_ETOOBIG);
    printf("tile 100x10 > side 64 -> ETOOBIG\n");

    /* 单块恰好占满。 */
    t.n = 1; t.t[0].w = 64; t.t[0].h = 64;
    CK(atlas_solve(&t, 0.0, &r) == ATLAS_OK);
    CK(r.side == 64 && r.waste == 0.0 && !r.pow2_rounded);
    printf("single 64x64 -> side 64 waste 0\n");

    /* 2 的幂向上取整：3 块 40x40，raw 约 70，应取 128。 */
    t.n = 3; t.t[0].w = t.t[1].w = t.t[2].w = 40;
    t.t[0].h = t.t[1].h = t.t[2].h = 40;
    CK(atlas_solve(&t, 1.0, &r) == ATLAS_OK);
    CK(r.side_raw > 64 && r.side_raw <= 120);
    CK(r.side == 128 && r.pow2_rounded == 1);
    printf("3x40x40 raw=%d -> side %d (rounded up)\n", r.side_raw, r.side);

    /* 全同尺寸 63 个 32x32：面积 64512，raw 在 255 附近，side 256。 */
    atlas_sample_uniform(&t);
    CK(atlas_solve(&t, 1.0, &r) == ATLAS_OK);
    CK(r.tiles == 63 && r.side == 256);
    printf("63x32x32 tile_area=%lld side=%d waste=%.4f\n",
           r.tile_area, r.side, r.waste);

    /* 浪费率上界不满足时必须报 EWASTE，而不是 OK。 */
    atlas_sample_mixed(&t);
    int c = atlas_solve(&t, 0.0, &r);
    CK(c == ATLAS_EWASTE && r.pass == 0 && r.packed == 1);
    printf("mixed with limit 0 -> EWASTE waste=%.4f\n", r.waste);

    /* 极端长条。 */
    atlas_sample_strips(&t);
    CK(atlas_solve(&t, 1.0, &r) == ATLAS_OK);
    CK(is_pow2(r.side));
    static int boxes[4 * ATLAS_MAX_TILES];
    CK(atlas_pack(&t, r.side, boxes, ATLAS_MAX_TILES) == 1);
    CK(layout_valid(&t, r.side, boxes, t.n));
    printf("strips side=%d raw=%d waste=%.4f\n", r.side, r.side_raw, r.waste);

    /* 最小边长恰好等于最大图块边：raw-1 因块放不进而不可行。 */
    t.n = 2; t.t[0].w = 100; t.t[0].h = 10; t.t[1].w = 20; t.t[1].h = 20;
    CK(atlas_solve(&t, 1.0, &r) == ATLAS_OK);
    CK(r.side_raw == 100);
    CK(atlas_pack(&t, r.side_raw - 1, NULL, 0) == ATLAS_ETOOBIG);
    printf("2 tiles max-dim 100 -> raw %d, raw-1 reports ETOOBIG\n\n", r.side_raw);

    /* 参数越界。 */
    t.n = 1; t.t[0].w = 8; t.t[0].h = 8;
    CK(atlas_solve(&t, -0.1, &r) == ATLAS_EINPUT);
    CK(atlas_solve(&t, 1.1, &r) == ATLAS_EINPUT);
    CK(atlas_pack(&t, 0, NULL, 0) == ATLAS_EINPUT);

    /* 四组样例在宽松上界下全部可行，并逐一校验布局与最小性。 */
    check_samples(1.0);

    /* 用足够大的上界验证每组样例都能验收。 */
    Tiles samples[4];
    atlas_sample_icons(&samples[0]);
    atlas_sample_mixed(&samples[1]);
    atlas_sample_strips(&samples[2]);
    atlas_sample_uniform(&samples[3]);
    for (int i = 0; i < 4; ++i) {
        AtlasResult rr;
        int rc = atlas_solve(&samples[i], 1.0, &rr);
        CK(rc == ATLAS_OK);
        CK(atlas_solve(&samples[i], rr.waste, &rr) == ATLAS_OK);
        CK(atlas_solve(&samples[i], rr.waste - 1e-9, &rr) == ATLAS_EWASTE);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
