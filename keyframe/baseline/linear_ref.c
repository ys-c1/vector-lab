/* 旧线性插值参照：与主实现没有任何共享代码，只负责产出可与主实现
 * 逐字节比对的节点表。节点布局约定（与主实现相同）：
 *   每段 levels 个间隔，段 i 的节点下标为 i*levels .. (i+1)*levels，
 *   关键帧落在段首节点；每段中点时刻取两端关键帧时刻的算术平均。
 * 用法：linear_ref <case> <out.txt>
 * 输出：首行 "keys levels nodes"，随后每个节点一行 "t x y"
 *       （%.17g，可精确往返），再每段一行 "mid t x y"。 */
#include <stdio.h>

#define MAXK 16
#define MAXL 16
#define MAXN (MAXK * MAXL)

typedef struct { double t, x, y; } KF;
typedef struct { const char *name; int nkeys, levels; KF k[8]; } Case;

/* 与 compat.c 内联表一致的若干固定用例。 */
static const Case cases[] = {
    {"square2", 5, 2, {
        {0.0,  0.0,  0.0}, {1.0, 10.0,  0.0}, {2.0, 10.0, 10.0},
        {3.0,  0.0, 10.0}, {4.0,  0.0,  0.0}
    }},
    {"square4", 5, 4, {
        {0.0,  0.0,  0.0}, {1.0, 10.0,  0.0}, {2.0, 10.0, 10.0},
        {3.0,  0.0, 10.0}, {4.0,  0.0,  0.0}
    }},
    {"uneven", 4, 3, {
        {0.0,  0.0,  0.0}, {0.7, 10.0,  0.0}, {2.0, 10.0, 10.0},
        {3.1,  0.0, 10.0}
    }},
    {"two", 2, 5, {
        {0.0, 0.0, 0.0}, {4.0, 8.0, 6.0}
    }},
};
#define NCASES ((int)(sizeof cases / sizeof cases[0]))

static int emit(const char *path, const Case *c) {
    double ts[MAXN], xs[MAXN], ys[MAXN];
    int n = c->nkeys, levels = c->levels;
    int N = (n - 1) * levels + 1;
    for (int j = 0; j < N; ++j) {
        int i = j / levels;
        int r = j % levels;
        if (i >= n - 1) {
            ts[j] = c->k[n - 1].t;
            xs[j] = c->k[n - 1].x;
            ys[j] = c->k[n - 1].y;
        } else {
            double f = (double)r / (double)levels;
            ts[j] = c->k[i].t + (c->k[i + 1].t - c->k[i].t) * f;
            xs[j] = c->k[i].x + (c->k[i + 1].x - c->k[i].x) * f;
            ys[j] = c->k[i].y + (c->k[i + 1].y - c->k[i].y) * f;
        }
    }
    FILE *fp = fopen(path, "w");
    if (!fp) return -1;
    fprintf(fp, "%d %d %d\n", n, levels, N);
    for (int j = 0; j < N; ++j) fprintf(fp, "%.17g %.17g %.17g\n", ts[j], xs[j], ys[j]);
    for (int i = 0; i < n - 1; ++i) {
        double tm = 0.5 * (c->k[i].t + c->k[i + 1].t);
        double f = (tm - c->k[i].t) / (c->k[i + 1].t - c->k[i].t);
        double mx = c->k[i].x + (c->k[i + 1].x - c->k[i].x) * f;
        double my = c->k[i].y + (c->k[i + 1].y - c->k[i].y) * f;
        fprintf(fp, "mid %.17g %.17g %.17g\n", tm, mx, my);
    }
    fclose(fp);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s case out.txt\n", argv[0]); return 1; }
    /* 名字按完整字符串匹配，避免前缀歧义 */
    int idx = -1;
    for (int i = 0; i < NCASES; ++i) {
        const char *a = argv[1], *b = cases[i].name;
        while (*a && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') { idx = i; break; }
    }
    if (idx < 0) { fprintf(stderr, "unknown case %s\n", argv[1]); return 2; }
    return emit(argv[2], &cases[idx]) != 0;
}
