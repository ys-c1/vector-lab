/* 原地分块批量变换的基准与证据程序。
 * 用法：
 *   bench gen <n> <in.txt>            生成 n 个确定性伪随机点的点文件
 *   bench run <in.txt> <out.txt> <chunk>
 *      读入点文件 -> xform_batch_inplace 原地变换 -> 写出，
 *      报告：点数、毫秒、峰值内存增量、数组字节数、二者比值。
 * 点文件格式：首行点数 n，随后 n 行 "x y"（与 gold/ref 一致）。
 * 拒绝：空数组（参数 n）、零块宽（参数 chunk）、文件行数与首行不符（参数 count）。
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime */
#include "xform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

/* 与 gold 参照共用同一组矩阵参数（命令行字符串形式，见 Makefile）。 */
static const XForm M = {0.8, -0.6, 0.6, 0.8, 12.5, -7.25};

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static long peak_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss; /* Linux:  KiB，进程历史峰值 */
}

static unsigned long long rng = 0x9e3779b97f4a7c15ull;
static double frand(void) { /* xorshift64*，确定性伪随机 */
    rng ^= rng >> 12; rng ^= rng << 25; rng ^= rng >> 27;
    return (double)(rng * 0x2545F4914F6CDD1Dull >> 11) / (double)(1ull << 52) * 2000.0 - 1000.0;
}

static int cmd_gen(size_t n, const char *path) {
    if (n == 0) {
        fprintf(stderr, "reject: empty array (parameter: n)\n");
        return 2;
    }
    FILE *f = fopen(path, "w");
    if (!f) { perror(path); return 3; }
    fprintf(f, "%zu\n", n);
    for (size_t i = 0; i < n; ++i) fprintf(f, "%.6f %.6f\n", frand(), frand());
    fclose(f);
    return 0;
}

static XPoint *load(const char *path, size_t *out_n) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return NULL; }
    size_t n = 0;
    if (fscanf(f, "%zu", &n) != 1 || n == 0) {
        fprintf(stderr, "reject: empty array (parameter: n)\n");
        fclose(f);
        return NULL;
    }
    XPoint *p = (XPoint *)malloc(n * sizeof *p);
    if (!p) { fclose(f); return NULL; }
    size_t got = 0;
    while (got < n && fscanf(f, "%lf %lf", &p[got].x, &p[got].y) == 2) ++got;
    double extra;
    int more = fscanf(f, "%lf", &extra); /* 尾部还有数 -> 行数超了 */
    fclose(f);
    if (got != n || more == 1) {
        fprintf(stderr, "reject: length mismatch (parameter: count) header=%zu actual=%zu%s\n",
                n, got, more == 1 ? "+" : "");
        free(p);
        return NULL;
    }
    *out_n = n;
    return p;
}

static int cmd_run(const char *in_path, const char *out_path, size_t chunk) {
    if (chunk == 0) {
        fprintf(stderr, "reject: zero chunk width (parameter: chunk)\n");
        return 2;
    }
    long base_kb = peak_kb();          /* 分配前峰值基线 */
    size_t n = 0;
    XPoint *p = load(in_path, &n);
    if (!p) return 2;

    double t0 = now_ms();
    const char *bad = xform_batch_inplace(M, p, n, chunk);
    double ms = now_ms() - t0;
    if (bad) {
        fprintf(stderr, "reject: bad argument (parameter: %s)\n", bad);
        free(p);
        return 2;
    }
    long peak = peak_kb();

    FILE *f = fopen(out_path, "w");
    if (!f) { perror(out_path); free(p); return 3; }
    fprintf(f, "%zu\n", n);
    for (size_t i = 0; i < n; ++i) fprintf(f, "%.6f %.6f\n", p[i].x, p[i].y);
    fclose(f);
    free(p);

    double array_mb = (double)n * sizeof(XPoint) / (1 << 20);
    double delta_mb = (peak - base_kb) / 1024.0;
    double ratio = delta_mb / array_mb;
    /* 无第二次拷贝的依据：峰值增量 ≈ 1 倍数组；若有中转副本则 ≥ 2 倍。 */
    printf("n=%zu chunk=%zu ms=%.3f peak_delta=%.2fMB array=%.2fMB ratio=%.2f nocopy=%s\n",
           n, chunk, ms, delta_mb, array_mb, ratio, ratio < 1.5 ? "YES" : "NO");
    return 0;
}

int main(int argc, char **argv) {
    if (argc >= 4 && !strcmp(argv[1], "gen"))
        return cmd_gen(strtoull(argv[2], 0, 10), argv[3]);
    if (argc >= 5 && !strcmp(argv[1], "run"))
        return cmd_run(argv[2], argv[3], strtoull(argv[4], 0, 10));
    fprintf(stderr, "usage: bench gen <n> <in.txt> | bench run <in.txt> <out.txt> <chunk>\n");
    return 1;
}
