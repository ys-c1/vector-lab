/* 等分点落盘工具：按累计弧长等分折线，点一个个写到 stdout（可重定向落盘）。
 * 用法：
 *   points <line.txt> <n>            输出 n 个等分点（与 gold/ref points 同格式）
 *   points <line.txt> --spacing <d>  按间距 d 取点
 * 输入格式：每行 "x y"。输出：首行点数，随后每行 "%.6f %.6f"。
 * 拒绝情形（退出码 2，stderr 说清是哪一项）：
 *   total-length-zero    总长为零
 *   segment-count-lt-2   段数小于二
 *   spacing-exceeds-total 间距比总长还大
 */
#include "arclen.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load(const char *path, APoint **out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int cap = 256, n = 0;
    APoint *p = (APoint *)malloc((size_t)cap * sizeof *p);
    if (!p) { fclose(f); return -1; }
    while (fscanf(f, "%lf %lf", &p[n].x, &p[n].y) == 2) {
        if (++n == cap) {
            cap *= 2;
            APoint *q = (APoint *)realloc(p, (size_t)cap * sizeof *q);
            if (!q) { free(p); fclose(f); return -1; }
            p = q;
        }
    }
    fclose(f);
    *out = p;
    return n;
}

static double total_len(const APoint *p, int n) {
    double total = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        total += sqrt(dx * dx + dy * dy);
    }
    return total;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: points <line.txt> <n> | points <line.txt> --spacing <d>\n");
        return 1;
    }
    APoint *p = NULL;
    int n = load(argv[1], &p);
    if (n < 0) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    if (n < 2) {
        fprintf(stderr, "REJECT polyline-too-short: 折线点数不足 (%d)\n", n);
        free(p);
        return 2;
    }
    double total = total_len(p, n);
    if (total == 0.0) {
        fprintf(stderr, "REJECT total-length-zero: 总长为零\n");
        free(p);
        return 2;
    }
    double spacing;
    if (!strcmp(argv[2], "--spacing")) {
        if (argc < 4) { fprintf(stderr, "usage: points <line.txt> --spacing <d>\n"); free(p); return 1; }
        spacing = atof(argv[3]);
        if (!(spacing > 0.0)) {
            fprintf(stderr, "REJECT spacing-not-positive: 间距非正 (%g)\n", spacing);
            free(p);
            return 2;
        }
        if (spacing > total) {
            fprintf(stderr, "REJECT spacing-exceeds-total: 间距比总长还大 (间距 %.6f > 总长 %.6f)\n",
                    spacing, total);
            free(p);
            return 2;
        }
        if ((int)floor(total / spacing + 1e-9) < 2) {
            fprintf(stderr, "REJECT segment-count-lt-2: 段数小于二 (间距 %.6f, 总长 %.6f)\n",
                    spacing, total);
            free(p);
            return 2;
        }
    } else {
        int count = atoi(argv[2]);
        if (count - 1 < 2) {
            fprintf(stderr, "REJECT segment-count-lt-2: 段数小于二 (点数 %d -> 段数 %d)\n",
                    count, count - 1);
            free(p);
            return 2;
        }
        spacing = total / (count - 1);
    }
    int need = arclen_even(p, n, spacing, NULL, 0);
    if (need < 0) {
        fprintf(stderr, "REJECT %s\n", arclen_strerror(need));
        free(p);
        return 2;
    }
    APoint *out = (APoint *)malloc((size_t)need * sizeof *out);
    if (!out) { free(p); return 1; }
    int got = arclen_even(p, n, spacing, out, need);
    if (got < 0) {
        fprintf(stderr, "REJECT %s\n", arclen_strerror(got));
        free(out); free(p);
        return 2;
    }
    printf("%d\n", got);
    for (int k = 0; k < got; ++k)
        printf("%.6f %.6f\n", out[k].x, out[k].y);
    free(out);
    free(p);
    return 0;
}
