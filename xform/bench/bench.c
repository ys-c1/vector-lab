/*
 * xform 批量变换基准与块宽扫描程序。
 *
 * 用法：
 *   bench sweep  <scenario> [reps]   块宽扫描，输出每点耗时与带宽
 *   bench mem    <scenario>          峰值内存（VmHWM 差值 + wrap 计数器）
 *   bench copy   <scenario>          二次拷贝对照实现的峰值内存与流量
 *   bench verify <scenario>          所有块宽结果与逐点参考实现逐位比对
 *
 * scenario: dense | sparse | nan | long
 *   dense  n=100000, stride=16        （稠密，10 万点验收工况）
 *   sparse n=100000, stride=64        （稀疏：每点独占一条 64B 缓存行）
 *   nan    n=100000, stride=16, 10% 坐标注入 NaN
 *   long   n=10000000, stride=16      （超长：320 MiB，超出 24 MiB L3）
 */
#define _GNU_SOURCE
#include "xform.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern unsigned long xb_alloc_calls;
extern size_t        xb_alloc_bytes;
extern unsigned long xb_copy_calls;
extern size_t        xb_copy_bytes;

/* 扫描点：4 的倍数（缓存行对齐），100 为刻意加入的非对齐对照 */
static const size_t WIDTHS[] = {
    4, 8, 16, 32, 64, 100, 128, 256, 512, 1024, 2048, 4096, 8192, 16384
};
#define NW (sizeof(WIDTHS) / sizeof(WIDTHS[0]))

typedef struct {
    const char *name;
    size_t n;
    size_t stride;       /* 字节 */
    int    inject_nan;
} Scenario;

static const Scenario SC[] = {
    {"dense",  100000,   16, 0},
    {"sparse", 100000,   64, 0},
    {"nan",    100000,   16, 1},
    {"long",   10000000, 16, 0},
};

static const Scenario *pick(const char *name) {
    for (size_t i = 0; i < sizeof(SC) / sizeof(SC[0]); ++i)
        if (strcmp(SC[i].name, name) == 0)
            return &SC[i];
    fprintf(stderr, "unknown scenario '%s'\n", name);
    exit(2);
}

/* 确定性填充：LCG，避免计时段内出现 page fault / 首次触碰噪声 */
static void fill(void *base, const Scenario *sc) {
    size_t slots = sc->n;
    unsigned char *p = base;
    uint64_t st = 0x9e3779b97f4a7c15ULL;
    for (size_t i = 0; i < slots; ++i) {
        double *xy = (double *)(p + i * sc->stride);
        st = st * 6364136223846793005ULL + 1442695040888963407ULL;
        xy[0] = (double)(int64_t)st * 1e-18;
        st = st * 6364136223846793005ULL + 1442695040888963407ULL;
        xy[1] = (double)(int64_t)st * 1e-18;
        if (sc->inject_nan && (i % 10 == 0))
            xy[0] = NAN;
    }
}

static void *alloc_buf(const Scenario *sc) {
    size_t bytes = sc->n * sc->stride;
    void *p = NULL;
    if (posix_memalign(&p, 64, bytes)) {
        perror("posix_memalign");
        exit(1);
    }
    memset(p, 0, bytes);      /* 预触页，计时段内不再产生缺页 */
    return p;
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static XForm make_matrix(void) {
    /* 一般位置仿射：旋转/缩放/平移，避免编译器把零项优化掉 */
    XForm m = {0.9238795, -0.3826834, 0.3826834, 0.9238795, 17.0, -29.0};
    return m;
}

/* 计时段：原地内核。返回秒（reps 次总时长）。
 * 每次重算前用同一 LCG 恢复缓冲区，恢复不计入。 */
static double time_inplace(void *buf, const Scenario *sc, size_t w, int reps) {
    XForm m = make_matrix();
    fill(buf, sc);
    double t0 = now_sec();
    for (int r = 0; r < reps; ++r)
        xform_apply_batch_strided(m, buf, sc->n, sc->stride, w);
    double t1 = now_sec();
    return t1 - t0;
}

static int cmp_double(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static int cmd_sweep(const Scenario *sc, int reps) {
    void *buf = alloc_buf(sc);
    size_t bytes = sc->n * sc->stride;
    printf("# scenario=%s n=%zu stride=%zu input_bytes=%zu reps=%d\n",
           sc->name, sc->n, sc->stride, bytes, reps);
    printf("# block_width  median_total_ms  ns_per_point  traffic_GB/s  "
           "alloc_calls  copy_calls\n");

    double best_ns = 1e300;
    size_t best_w = 0;
    for (size_t k = 0; k < NW; ++k) {
        size_t w = WIDTHS[k];
        double *t = malloc(sizeof(double) * (size_t)reps);
        /* reps 次独立测量，每次重新填充以保持相同缓存初始状态 */
        for (int r = 0; r < reps; ++r) {
            xb_alloc_calls = xb_copy_calls = 0;   /* 只统计本次内核调用 */
            xb_alloc_bytes = xb_copy_bytes = 0;
            t[r] = time_inplace(buf, sc, w, 1);
        }
        qsort(t, (size_t)reps, sizeof(double), cmp_double);
        double med = t[reps / 2];
        free(t);
        double ns_pt = med * 1e9 / (double)sc->n;
        /* 每点经内存系统搬运 32B（16B 读 + 16B 写），
         * 稀疏时实际占用 64B 缓存行，有效载荷率 16/64 */
        double moved = (double)sc->n * 32.0;
        double gbps = moved / med / 1e9;
        if (ns_pt < best_ns) { best_ns = ns_pt; best_w = w; }
        printf("%7zu %14.4f %13.4f %12.2f %11lu %11lu\n",
               w, med * 1e3, ns_pt, gbps, xb_alloc_calls, xb_copy_calls);
    }
    printf("# best_width=%zu best_ns_per_point=%.4f\n", best_w, best_ns);
    free(buf);
    return 0;
}

static long vmhwm_kb(void) {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f)
        return -1;
    char line[256];
    long hwm = -1;
    while (fgets(line, sizeof(line), f))
        if (strncmp(line, "VmHWM:", 6) == 0) {
            sscanf(line + 6, "%ld", &hwm);
            break;
        }
    fclose(f);
    return hwm;
}

static int cmd_mem(const Scenario *sc) {
    long hwm0 = vmhwm_kb();
    xb_alloc_calls = xb_copy_calls = 0;
    xb_alloc_bytes = xb_copy_bytes = 0;
    void *buf = alloc_buf(sc);
    size_t bytes = sc->n * sc->stride;
    long hwm1 = vmhwm_kb();

    xb_alloc_calls = xb_copy_calls = 0;   /* 只统计变换期间 */
    xb_alloc_bytes = xb_copy_bytes = 0;
    xform_apply_batch_strided(make_matrix(), buf, sc->n, sc->stride, 256);
    long hwm2 = vmhwm_kb();
    free(buf);

    long peak_kb = hwm2 - hwm0;
    printf("scenario=%s n=%zu input_bytes=%zu (%.2f MiB)\n",
           sc->name, sc->n, bytes, bytes / 1048576.0);
    printf("VmHWM_before_alloc=%ld KiB  after_alloc=%ld KiB  after_xform=%ld KiB\n",
           hwm0, hwm1, hwm2);
    printf("peak_attributable=%ld KiB (%.2f MiB)  peak/input=%.3f\n",
           peak_kb, peak_kb / 1024.0, (peak_kb * 1024.0) / bytes);
    printf("during_xform: alloc_calls=%lu alloc_bytes=%zu "
           "copy_calls=%lu copy_bytes=%zu\n",
           xb_alloc_calls, xb_alloc_bytes, xb_copy_calls, xb_copy_bytes);
    return 0;
}

static int cmd_copy(const Scenario *sc) {
    long hwm0 = vmhwm_kb();
    size_t npt_bytes = sc->n * sizeof(XPoint);
    XPoint *src = NULL, *dst = NULL;
    if (posix_memalign((void **)&src, 64, npt_bytes) ||
        posix_memalign((void **)&dst, 64, npt_bytes)) {
        perror("posix_memalign");
        return 1;
    }
    memset(src, 0, npt_bytes);
    memset(dst, 0, npt_bytes);
    /* 对照内核只接受稠密 XPoint 数组：填充时强制 16B 步长 */
    Scenario dsc = *sc;
    dsc.stride = sizeof(XPoint);
    fill(src, &dsc);
    long hwm1 = vmhwm_kb();
    double t0 = now_sec();
    xform_apply_batch_copy(make_matrix(), src, dst, sc->n, 256);
    double t1 = now_sec();
    long hwm2 = vmhwm_kb();
    long peak_kb = hwm2 - hwm0;
    printf("scenario=%s (copy baseline, dense 16B/point layout) n=%zu input_bytes=%zu\n",
           sc->name, sc->n, npt_bytes);
    printf("VmHWM before/after/run: %ld/%ld/%ld KiB\n", hwm0, hwm1, hwm2);
    printf("peak_attributable=%ld KiB  peak/input=%.3f (two full arrays)\n",
           peak_kb, (peak_kb * 1024.0) / npt_bytes);
    printf("copy_kernel_time=%.4f ms  traffic=%.2f MB (48B/point)\n",
           (t1 - t0) * 1e3, (double)sc->n * 48.0 / 1e6);
    free(src);
    free(dst);
    return 0;
}

/* 逐位校验：所有块宽、所有场景与标量参考实现结果一致（NaN 用 isnan 比较） */
static int cmd_verify(const Scenario *sc) {
    void *buf = alloc_buf(sc);
    XPoint *ref = malloc(sc->n * sizeof(XPoint));
    fill(buf, sc);
    const XPoint *aspt = buf;
    XForm m = make_matrix();
    for (size_t i = 0; i < sc->n; ++i) {
        double x = (sc->stride == 16) ? aspt[i].x
                   : *(double *)((unsigned char *)buf + i * sc->stride);
        double y = (sc->stride == 16) ? aspt[i].y
                   : *(double *)((unsigned char *)buf + i * sc->stride + 8);
        ref[i] = xform_apply(m, (XPoint){x, y});
    }
    int bad = 0;
    for (size_t k = 0; k < NW; ++k) {
        fill(buf, sc);
        xform_apply_batch_strided(m, buf, sc->n, sc->stride, WIDTHS[k]);
        for (size_t i = 0; i < sc->n; ++i) {
            const double *xy =
                (const double *)((const unsigned char *)buf + i * sc->stride);
            int okx = isnan(ref[i].x) ? isnan(xy[0]) : xy[0] == ref[i].x;
            int oky = isnan(ref[i].y) ? isnan(xy[1]) : xy[1] == ref[i].y;
            if (!okx || !oky) {
                printf("MISMATCH w=%zu i=%zu got %.17g/%.17g want %.17g/%.17g\n",
                       WIDTHS[k], i, xy[0], xy[1], ref[i].x, ref[i].y);
                        if (++bad > 5) goto done;
            }
        }
    }
done:
    printf("scenario=%s verify=%s\n", sc->name, bad ? "FAIL" : "OK");
    free(ref);
    free(buf);
    return bad ? 1 : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s sweep|mem|copy|verify <scenario> [reps]\n",
                argv[0]);
        return 2;
    }
    const Scenario *sc = pick(argv[2]);
    if (strcmp(argv[1], "sweep") == 0) {
        int reps = argc > 3 ? atoi(argv[3]) : 11;
        return cmd_sweep(sc, reps);
    }
    if (strcmp(argv[1], "mem") == 0)
        return cmd_mem(sc);
    if (strcmp(argv[1], "copy") == 0)
        return cmd_copy(sc);
    if (strcmp(argv[1], "verify") == 0)
        return cmd_verify(sc);
    fprintf(stderr, "unknown mode '%s'\n", argv[1]);
    return 2;
}
