/* 交叉验证：关闭不动点逻辑（flags=0）时，warp 的输出必须与改动前的
 * 固定 8 次修正路径逐位一致——包括求出的 t、映射点和最终 PGM 字节。
 * 两条路径调用同一库内的真实函数，而不是复刻公式。 */
#include "warp.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static WPoint kCases[][4] = {
    {{0,0},{0,14},{10,-10},{10,6}},     /* 强变形基准曲线（非对称） */
    {{0,0},{1,2},{2,2},{3,0}},          /* 普通拱起曲线 */
    {{5,5},{5.5,6},{6.5,4},{7,5}},      /* 短曲线 */
    {{0,0},{-2,3},{8,3},{10,0}},        /* S 形 */
};
#define NCASES (int)(sizeof kCases / sizeof kCases[0])

int main(void) {
    double t0s[4] = {0.0, 0.25, 0.5, 1.0};

    for (int ci = 0; ci < NCASES; ++ci) {
        WPoint *c = kCases[ci];
        double total = warp_arclen(c, 1.0);
        CK(total > 0.0);

        for (int gi = 0; gi < 9; ++gi) {
            double s = total * (double)gi / 8.0;
            for (int k = 0; k < 4; ++k) {
                /* 旧路径：固定 8 次 */
                double tl;
                CK(warp_correct_legacy(c, s, t0s[k], &tl) == WARP_OK);
                /* 新接口但关掉不动点逻辑：cfg 给一组"若被误用就会暴露"的参数 */
                WarpCfg cfg = {1e-13, 1e-15, 2, 0.0, 0.8};
                WPoint p; WarpReport r;
                CK(warp_correct(c, s, t0s[k], 0, &cfg, &p, &r) == WARP_OK);
                CK(r.iters == 8);
                CK(r.t == tl);                 /* t 逐位相同 */
                CK(r.status == WARP_OK);
            }
        }

        /* 整条等分序列：flags=0 与 legacy 逐点相同，PGM 逐字节相同 */
        enum { N = 13 };
        WPoint a[N], b[N];
        CK(warp_equally_spaced(c, N, 0, 0, a, 0) == WARP_OK);
        WarpCfg cfg = {1e-12, 1e-14, 128, 0.0, 0.8};
        CK(warp_equally_spaced(c, N, 0, &cfg, b, 0) == WARP_OK);
        int same = 1;
        for (int i = 0; i < N; ++i)
            if (a[i].x != b[i].x || a[i].y != b[i].y) same = 0;
        CK(same);

        CK(warp_save_pgm("build/compat_a.pgm", a, N, -1, -11, 11, 15, 100, 100) == WARP_OK);
        CK(warp_save_pgm("build/compat_b.pgm", b, N, -1, -11, 11, 15, 100, 100) == WARP_OK);
    }

    printf("%d checks, %d failures\n", checks, fails);
    if (fails) return 1;

    /* PGM 字节级比对（最后一条曲线的两份产物）。 */
    FILE *fa = fopen("build/compat_a.pgm", "rb");
    FILE *fb = fopen("build/compat_b.pgm", "rb");
    CK(fa && fb);
    int byte_same = 1;
    if (fa && fb) {
        unsigned char ba[4096], bb[4096];
        size_t na, nb;
        do {
            na = fread(ba, 1, sizeof ba, fa);
            nb = fread(bb, 1, sizeof bb, fb);
            if (na != nb || memcmp(ba, bb, na) != 0) { byte_same = 0; break; }
        } while (na > 0);
        fclose(fa); fclose(fb);
    }
    CK(byte_same);
    printf(byte_same ? "PGM bitwise identical\n" : "PGM MISMATCH\n");
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
