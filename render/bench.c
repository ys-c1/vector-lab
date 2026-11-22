/* 掩码布点基准：掩码校验、四角度组的精确花纹周期（晶格整数推导）与渲染耗时。
 * 构建：make -C render bench   运行：./render/build/bench
 */
#include "src/scene.h"
#include "src/raster.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

/* 整数边 a*x + b*y = c 下，第 y 行边界在 4x4 子晶格中的相位结构。
 * 子点 (col,row) 翻转条件：a*(4x+2col+1)+b*(4y+2row+1) <= 4c，
 * 结构只取决于分子 T 模 (4a)，故用 T%(4a) 作为该行的离散相位。 */
static void row_phase(int a, int b, int c, int y, const int *pts, int n, long *out) {
    for (int k = 0; k < n; ++k) {
        int col = pts[2 * k], row = pts[2 * k + 1];
        long T = 4L * c - (long)b * (4 * y + 2 * row + 1) - (long)a * (2 * col + 1);
        long m = 4L * a;
        out[k] = T % m;
        if (out[k] < 0) out[k] += m;
    }
}

static int cmp_long(const void *p, const void *q) {
    long x = *(const long *)p, y = *(const long *)q;
    return (x > y) - (x < y);
}

/* 沿 y 的精确重复周期：对每行的有序相位结构取哈希，再用 KMP 前缀函数
 * 求哈希序列的最小周期，最后对候选做逐元素复核（排除哈希碰撞）。 */
static unsigned long long row_hash(const long *v, int n) {
    unsigned long long h = 1469598103934665603ULL;
    for (int k = 0; k < n; ++k) {
        h ^= (unsigned long long)v[k] + 0x9e3779b97f4a7c15ULL;
        h *= 1099511628211ULL;
    }
    return h;
}

static int rows_equal(long all[][16], int n, int y1, int y2) {
    for (int k = 0; k < n; ++k)
        if (all[y1][k] != all[y2][k]) return 0;
    return 1;
}

static int lattice_period(int a, int b, int c, const int *pts, int n, int rows) {
    long all[256][16];
    unsigned long long h[256];
    for (int y = 0; y < rows; ++y) {
        row_phase(a, b, c, y, pts, n, all[y]);
        qsort(all[y], n, sizeof(long), cmp_long);
        h[y] = row_hash(all[y], n);
    }
    /* KMP 前缀函数：pi[i] = h[0..i] 的最长相等真前后缀长度 */
    int pi[256] = {0};
    for (int i = 1; i < rows; ++i) {
        int j = pi[i - 1];
        while (j > 0 && h[i] != h[j]) j = pi[j - 1];
        if (h[i] == h[j]) ++j;
        pi[i] = j;
    }
    int cand = rows - pi[rows - 1];
    /* 几何相位序列从第 0 行起严格周期，故候选无需整除窗口长；逐元素复核排除碰撞 */
    for (int y = cand; y < rows; ++y)
        if (!rows_equal(all, n, y, y - cand)) return rows; /* 碰撞兜底 */
    return cand;
}

static void mask_points(unsigned short mask, int *pts, int *n) {
    *n = 0;
    for (int b = 0; b < RMASK_BITS; ++b)
        if (mask & (1u << b)) { pts[2 * *n] = b & 3; pts[2 * *n + 1] = b >> 2; (*n)++; }
}

static void print_mask(unsigned short mask) {
    for (int row = 0; row < 4; ++row) {
        printf("  ");
        for (int col = 0; col < 4; ++col) putchar((mask >> (4 * row + col)) & 1 ? '1' : '0');
        putchar('\n');
    }
}

/* 构造一条斜率 p/q 的巨型三角形边，第三个顶点在法线一侧，窗口仍是 0..256。 */
static void slope_scene(RScene *s, int p, int q) {
    RPoint v[3] = {
        {-100000.0 * q, -100000.0 * p},
        { 100000.0 * q,  100000.0 * p},
        {-100000.0 * p,  100000.0 * q},
    };
    scene_init(s);
    scene_add_poly(s, v, 3);
}

static double time_ms(int (*fn)(void), int iters) {
    clock_t t0 = clock();
    for (int i = 0; i < iters; ++i) fn();
    return 1000.0 * (clock() - t0) / CLOCKS_PER_SEC;
}

/* 四角度组（精确有理斜率 |dy|/|dx| = p/q）。 */
static const struct { int ang, p, q; } GROUPS[4] = {
    {15, 7, 26}, {30, 4, 7}, {60, 7, 4}, {75, 26, 7},
};
/* 四个 8 置位、空间分散的掩码（行优先 4x4，见 print_mask）。 */
static const struct { const char *name; unsigned short mask; } MASKS[4] = {
    {"mask-A", 0xC9A5},
    {"mask-B", 0xA5C9},
    {"mask-C", 0x6996},
    {"mask-D", 0x9669},
};

static RScene scene;
static unsigned char buf[256 * 256];
static const RBox win = {0, 0, 256, 256};
static unsigned short cur_mask;

static int run_grid(void) { return raster_fill(&scene, &win, 256, 256, 4, buf); }
static int run_mask(void) { return raster_fill_mask(&scene, &win, 256, 256, cur_mask, buf); }

int main(void) {
    int allpts[32], nall = 16, k = 0;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col) { allpts[2*k] = col; allpts[2*k+1] = row; ++k; }

    /* ---------- 1. 掩码校验：位数不对 / 非法字符 / 全零 / 全一 ---------- */
    printf("== 掩码校验 ==\n");
    const char *cases[][2] = {
        {"101010", "短于16位"}, {"10101010101010101", "长于16位"},
        {"101x101010101010", "含非法字符"}, {"0000000000000000", "全零"},
        {"1111111111111111", "全一"}, {"1001111001101011", "合法16位"},
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        unsigned short m = 0;
        int rc = raster_mask_parse(cases[i][0], &m);
        if (rc == 0)
            printf("  [%s] %-18s 接受 0x%04X popcount=%d%s\n", cases[i][1], cases[i][0],
                   m, raster_mask_count(m),
                   raster_mask_count(m) == 0 ? "  <== 全零：无采样点，渲染拒绝(-4)" :
                   raster_mask_count(m) == 16 ? "  <== 全一：退化为16点满网格" : "");
        else
            printf("  [%s] %-18s 拒绝 rc=%d（%s）\n", cases[i][1], cases[i][0], rc,
                   rc == -2 ? "位数不对，必须恰好16位" : "含非0/1字符");
    }
    /* 显式验证全零掩码被渲染路径拒绝 */
    unsigned short zero = 0, one = 0xFFFF;
    raster_mask_parse("0000000000000000", &zero);
    raster_mask_parse("1111111111111111", &one);
    RScene dummy; slope_scene(&dummy, 1, 1);
    printf("  全零渲染 raster_fill_mask -> rc=%d（预期 -4）\n",
           raster_fill_mask(&dummy, &win, 8, 8, zero, buf));
    printf("  全一 popcount=%d（单独标记：等同等分满网格）\n\n", raster_mask_count(one));

    /* ---------- 2. 四角度组：掩码 / 花纹周期 / 耗时 ---------- */
    const int ROWS = 200, ITERS = 200;
    for (int g = 0; g < 4; ++g) {
        int p = GROUPS[g].p, q = GROUPS[g].q, a = p, b = -q, c = 400;
        printf("== 角度组 %d 度（精确斜率 %d/%d）==\n", GROUPS[g].ang, p, q);
        int grid_per = lattice_period(a, b, c, allpts, nall, ROWS);

        slope_scene(&scene, p, q);
        double t_grid = time_ms(run_grid, ITERS);
        printf("  等分4x4满网格  花纹周期=%3d 行  耗时=%8.2f ms/%d帧\n",
               grid_per, t_grid, ITERS);

        for (int mi = 0; mi < 4; ++mi) {
            unsigned short m = MASKS[mi].mask;
            int pts[32], n;
            mask_points(m, pts, &n);
            int per = lattice_period(a, b, c, pts, n, ROWS);
            cur_mask = m;
            double t = time_ms(run_mask, ITERS);
            printf("  %s popcount=%d 花纹周期=%3d 行（vs等分 %d，%s）耗时=%8.2f ms\n",
                   MASKS[mi].name, n, per, grid_per,
                   per < grid_per ? "更短" : per == grid_per ? "相等" : "更长", t);
            print_mask(m);
        }
        putchar('\n');
    }

    /* ---------- 3. 结论性穷举提示（周期下界由晶格决定）---------- */
    printf("== 周期下界检验（对该组穷举全部 65536 个掩码的最小可达周期）==\n");
    for (int g = 0; g < 4; ++g) {
        int p = GROUPS[g].p, q = GROUPS[g].q, a = p, b = -q, c = 400;
        int grid_per = lattice_period(a, b, c, allpts, nall, ROWS);
        int best = 64 + 1;
        for (int mm = 1; mm < 65536; ++mm) {
            int pts[32], n;
            mask_points((unsigned short)mm, pts, &n);
            int per = lattice_period(a, b, c, pts, n, 64); /* 周期上界 26，64 行足够 */
            if (per < best) best = per;
        }
        printf("  %2d度 斜率 %d/%d: 等分周期=%3d，全部掩码最小可达=%3d，能更短的掩码数=%s\n",
               GROUPS[g].ang, p, q, grid_per, best,
               best < grid_per ? ">0" : "0（不存在）");
    }
    return 0;
}
