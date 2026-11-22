/* 交点表：两种谓词各出一份表，供逐字节比对。
 *   table legacy <segs.txt>          冻结的固定阈值路径（isect_seg, 1e-12）
 *   table gap    <segs.txt> <tol>    新路径：isect_gap <= tol 判交
 * 退化（零长）身份取同目录 manifest.txt，缺失则回退坐标相等判定。
 * 输出每对一行 "i j gap"（legacy 模式 gap 列固定 0.000000e+00），末行统计。
 */
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXN 300

static int load(const char *path, IPoint *a0, IPoint *a1) {
    FILE *f = fopen(path, "r");
    if (!f) { perror("fopen"); return -1; }
    int n = 0;
    while (n < MAXN && fscanf(f, "%lf %lf %lf %lf",
                              &a0[n].x, &a0[n].y, &a1[n].x, &a1[n].y) == 4)
        ++n;
    fclose(f);
    return n;
}

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
    int idx;
    while (fscanf(f, "%d", &idx) == 1) {
        if (idx >= 0 && idx < n) zflag[idx] = 1;
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s legacy|gap <segs.txt> [tol]\n", argv[0]); return 1; }
    int gap_mode;
    if (!strcmp(argv[1], "legacy")) gap_mode = 0;
    else if (!strcmp(argv[1], "gap")) gap_mode = 1;
    else { fprintf(stderr, "bad mode\n"); return 1; }
    double tol = 0.0;
    if (gap_mode) {
        if (argc < 4) { fprintf(stderr, "gap mode needs tol\n"); return 1; }
        tol = atof(argv[3]);
    }
    static IPoint a0[MAXN], a1[MAXN];
    static char zflag[MAXN];
    int n = load(argv[2], a0, a1);
    if (n < 2) return 2;
    load_manifest(argv[2], n, zflag);

    int coord_zero = 0;
    for (int i = 0; i < n; ++i)
        if (isect_zero_len(a0[i], a1[i])) ++coord_zero;
    int zero = 0;
    for (int i = 0; i < n; ++i) if (zflag[i]) ++zero;
    if (zero == 0) zero = coord_zero;   /* 无 manifest 的回退口径 */

    int pairs = 0, zpairs = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            int hit;
            double g = 0.0;
            if (gap_mode) {
                g = isect_gap(a0[i], a1[i], a0[j], a1[j]);
                hit = g <= tol;
            } else {
                hit = isect_seg(a0[i], a1[i], a0[j], a1[j], 0);
            }
            if (!hit) continue;
            ++pairs;
            if (zflag[i] || zflag[j] ||
                isect_zero_len(a0[i], a1[i]) || isect_zero_len(a0[j], a1[j])) ++zpairs;
            printf("%d %d %.6e\n", i, j, g);
        }
    printf("n=%d pairs=%d zero_segs=%d zero_pairs=%d mode=%s\n",
           n, pairs, zero, zpairs, gap_mode ? "gap" : "legacy");
    return 0;
}
