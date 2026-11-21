#include "pick.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static unsigned long long rng_state = 88172645463325252ULL;
static double frand(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(rng_state >> 11) * (1.0 / 9007199254740992.0);
}

int main(void) {
    /* 基本最近命中：x=3 的段比 x=5 先命中 */
    PSeg segs[3] = {
        {{5, -1}, {5, 1}},
        {{3, -1}, {3, 1}},
        {{0, 5}, {8, 5}},
    };
    PIndex idx;
    CK(pick_index_build(segs, 3, &idx, 0) == PICK_OK);
    PPoint o = {0, 0}, d = {1, 0};
    PHit h;
    PStats st;
    CK(pick_ray_nearest(&idx, o, d, 1, &h, &st) == PICK_OK);
    CK(h.seg == 1 && h.t == 3.0 && h.at.x == 3.0 && h.at.y == 0.0);
    CK(st.boxes_tested == 3 && st.segs_tested == 2);
    CK(pick_ray_nearest(&idx, o, d, 0, &h, &st) == PICK_OK);
    CK(h.seg == 1 && st.boxes_tested == 0 && st.segs_tested == 3);

    /* 反向射线无命中 */
    PPoint back = {-1, 0};
    CK(pick_ray_nearest(&idx, o, back, 1, &h, &st) == PICK_OK && h.seg == -1);

    /* 全部命中 */
    PHit hits[8];
    int nh = pick_ray_all(&idx, o, d, 1, hits, 8, &st);
    CK(nh == 2 && hits[0].seg == 0 && hits[1].seg == 1); /* 按段号升序 */

    /* 零方向报错 */
    PPoint z = {0, 0};
    CK(pick_ray_nearest(&idx, o, z, 1, &h, &st) == PICK_ERR_DIR);
    CK(pick_ray_all(&idx, o, z, 1, hits, 8, &st) == PICK_ERR_DIR);

    /* 退化段报错并指出段号 */
    PSeg bad[3] = {
        {{0, 0}, {1, 0}},
        {{2, 2}, {2, 2}},
        {{0, 1}, {1, 1}},
    };
    PIndex bidx;
    int err_seg = -1;
    CK(pick_index_build(bad, 3, &bidx, &err_seg) == PICK_ERR_DEGEN);
    CK(err_seg == 1);

    /* 参数校验 */
    CK(pick_index_build(0, 3, &bidx, 0) == PICK_ERR_ARG);
    CK(pick_ray_nearest(0, o, d, 1, &h, &st) == PICK_ERR_ARG);

    /* 随机场景：剔除开关命中集合必须一致 */
    enum { N = 500, R = 50 };
    PSeg *rs = malloc(sizeof(PSeg) * N);
    for (int i = 0; i < N; ++i) {
        double cx = frand() * 100, cy = frand() * 100, a = frand() * 6.283185307179586;
        double hx = 2.0 * (1.0 + frand()), hy = hx;
        rs[i].p0.x = cx - hx * (a > 3.0 ? 1.0 : 0.3); rs[i].p0.y = cy - hy * 0.7;
        rs[i].p1.x = cx + hx * 0.4; rs[i].p1.y = cy + hy * (a > 3.0 ? 1.0 : 0.2);
    }
    PIndex ridx;
    CK(pick_index_build(rs, N, &ridx, 0) == PICK_OK);
    PHit *ha = malloc(sizeof(PHit) * N), *hb = malloc(sizeof(PHit) * N);
    for (int r = 0; r < R; ++r) {
        PPoint ro = {frand() * 100, frand() * 100};
        double ang = frand() * 6.283185307179586;
        PPoint rd = {0.6 * (ang > 3.0 ? 1.0 : -1.0), 0.8};
        int na = pick_ray_all(&ridx, ro, rd, 0, ha, N, 0);
        int nb = pick_ray_all(&ridx, ro, rd, 1, hb, N, 0);
        CK(na == nb);
        int same = 1;
        for (int i = 0; i < na; ++i)
            if (ha[i].seg != hb[i].seg || ha[i].t != hb[i].t || ha[i].u != hb[i].u) same = 0;
        CK(same);
        PHit n1, n0;
        CK(pick_ray_nearest(&ridx, ro, rd, 1, &n1, 0) == PICK_OK);
        CK(pick_ray_nearest(&ridx, ro, rd, 0, &n0, 0) == PICK_OK);
        CK(n1.seg == n0.seg && n1.t == n0.t);
    }

    /* 命中表导出 */
    CK(pick_hits_export(hb, 2, "build/test_hits.csv") == PICK_OK);
    FILE *f = fopen("build/test_hits.csv", "r");
    CK(f != 0);
    if (f) {
        int lines = 0, ch;
        while ((ch = fgetc(f)) != EOF) if (ch == '\n') ++lines;
        fclose(f);
        CK(lines == 3); /* 表头 + 2 行 */
    }
    CK(pick_hits_export(hb, 2, "/nonexistent-dir/x.csv") == PICK_ERR_ARG);

    free(ha); free(hb);
    pick_index_free(&ridx);
    free(rs);
    pick_index_free(&idx);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
