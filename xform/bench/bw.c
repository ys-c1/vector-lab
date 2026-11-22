/* 可用带宽锚点：纯流式读写微基准（与变换内核同机、同核、同编译器）。
 * 读 16B + 写 16B/点，无计算，测量该核实际能拿到的有效带宽。 */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(void) {
    const size_t n = 10000000;              /* 与 long 场景同规模：152.6 MiB */
    double *a = NULL;
    if (posix_memalign((void **)&a, 64, n * 16)) return 1;
    memset(a, 1, n * 16);
    /* 预热触页 */
    for (size_t i = 0; i < n * 2; ++i) a[i] += 1.0;

    double best = 1e300;
    for (int r = 0; r < 9; ++r) {
        double t0 = now_sec();
        double s = 0.0;
        for (size_t i = 0; i < n; ++i) {
            double x = a[2 * i], y = a[2 * i + 1];
            a[2 * i] = x + 1.0;
            a[2 * i + 1] = y + 1.0;
            s += x + y;
        }
        double t1 = now_sec();
        if (t1 - t0 < best) best = t1 - t0;
        if (s == 12345.6789) printf("x");   /* 防优化 */
    }
    double bytes = (double)n * 32.0;       /* 16B 读 + 16B 写 */
    printf("stream 16R+16W/point: best=%.4f ms  %.2f GB/s  %.4f ns/point\n",
           best * 1e3, bytes / best / 1e9, best * 1e9 / (double)n);
    free(a);
    return 0;
}
