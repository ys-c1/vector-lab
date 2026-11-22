#include "metric.h"

/* 逐级跳变：相邻两帧的差取整幅平均。用整数累加，长序列下不漂移。 */
double metric_jump(const unsigned char *a, const unsigned char *b, size_t n) {
    if (!a || !b || n == 0) return 0.0;
    unsigned long long sum = 0;
    for (size_t i = 0; i < n; ++i) {
        int d = (int)a[i] - (int)b[i];
        sum += (unsigned long long)(d < 0 ? -d : d);
    }
    return (double)sum / (double)n;
}

double metric_sequence_max_jump(const unsigned char *const *frames, size_t n, int count) {
    if (!frames || count < 2) return 0.0;
    double mx = 0.0;
    for (int k = 0; k + 1 < count; ++k) {
        double j = metric_jump(frames[k], frames[k + 1], n);
        if (j > mx) mx = j;
    }
    return mx;
}

double metric_sequence_mean_jump(const unsigned char *const *frames, size_t n, int count) {
    if (!frames || count < 2) return 0.0;
    double sum = 0.0;
    for (int k = 0; k + 1 < count; ++k) sum += metric_jump(frames[k], frames[k + 1], n);
    return sum / (double)(count - 1);
}
