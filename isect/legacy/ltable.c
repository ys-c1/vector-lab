/* legacy 交点表：只链接 legacy/ 下冻结的 isect 源码，固定阈值 1e-12。
 * 退化身份取同目录 manifest.txt（与 table.c 共用），缺失则回退坐标相等。
 * 输出格式与 table.c 的 legacy 模式逐字节一致，供逐字节比对。
 * 用法：ltable <segs.txt>
 */
#include "isect.h"
#include <stdio.h>
#include <string.h>

#define MAXN 300

static void load_manifest(const char *segs_path, int n, char *zflag) {
    char path[1024];
    const char *slash = strrchr(segs_path, '/');
    size_t dlen = slash ? (size_t)(slash - segs_path) + 1 : 0;
    snprintf(path, sizeof path, "%.*smanifest.txt", (int)dlen, segs_path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    int idx;
    while (fscanf(f, "%d", &idx) == 1)
        if (idx >= 0 && idx < n) zflag[idx] = 1;
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s <segs.txt>\n", argv[0]); return 1; }
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 2; }
    static IPoint a0[MAXN], a1[MAXN];
    static char zflag[MAXN];
    int n = 0;
    while (n < MAXN && fscanf(f, "%lf %lf %lf %lf",
                              &a0[n].x, &a0[n].y, &a1[n].x, &a1[n].y) == 4)
        ++n;
    fclose(f);
    load_manifest(argv[1], n, zflag);

    int coord_zero = 0;
    for (int i = 0; i < n; ++i)
        if (a0[i].x == a1[i].x && a0[i].y == a1[i].y) ++coord_zero;
    int zero = 0;
    for (int i = 0; i < n; ++i) if (zflag[i]) ++zero;
    if (zero == 0) zero = coord_zero;

    int pairs = 0, zpairs = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            if (!isect_seg(a0[i], a1[i], a0[j], a1[j], 0)) continue;
            ++pairs;
            if (zflag[i] || zflag[j] ||
                (a0[i].x == a1[i].x && a0[i].y == a1[i].y) ||
                (a0[j].x == a1[j].x && a0[j].y == a1[j].y)) ++zpairs;
            printf("%d %d %.6e\n", i, j, 0.0);
        }
    printf("n=%d pairs=%d zero_segs=%d zero_pairs=%d mode=legacy\n",
           n, pairs, zero, zpairs);
    return 0;
}
