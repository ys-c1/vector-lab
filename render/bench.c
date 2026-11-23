/* 三档画幅基准：缓存命中率、毫秒、内存峰值；并验证
 *   - 缓存开/关输出逐字节一致
 *   - 图块尺寸为零被拒绝
 *   - 键冲突计数为零（否则判失败）
 */
#define _POSIX_C_SOURCE 199309L
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/tilecache.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

#define PI 3.14159265358979323846

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static long peak_kb(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_maxrss;
}

/* 确定性场景：48 边形大圆（凸）+ 角部旋转方块（凸）+ 凹五角星 */
static void build_scene(RScene *s) {
    scene_init(s);
    RPoint pts[64];
    for (int i = 0; i < 48; ++i) {
        double a = 2.0 * PI * i / 48.0;
        pts[i].x = 1000.0 * cos(a);
        pts[i].y = 1000.0 * sin(a);
    }
    scene_add_poly(s, pts, 48);
    for (int i = 0; i < 4; ++i) {
        double a = 0.3 + (PI / 2.0) * i;
        pts[i].x = -700.0 + 180.0 * cos(a);
        pts[i].y = 700.0 + 180.0 * sin(a);
    }
    scene_add_poly(s, pts, 4);
    for (int i = 0; i < 10; ++i) {
        double a = 2.0 * PI * i / 10.0;
        double r = (i & 1) ? 60.0 : 150.0;
        pts[i].x = 700.0 + r * cos(a);
        pts[i].y = -700.0 + r * sin(a);
    }
    scene_add_poly(s, pts, 10);
}

int main(int argc, char **argv) {
    int fails = 0;
    TileCache bad;
    if (tc_init(&bad, 0, 64) == TC_ERR_ARG && tc_init(&bad, 64, 0) == TC_ERR_ARG &&
        tc_init(&bad, -8, -8) == TC_ERR_ARG)
        printf("ok: tile size zero/negative rejected\n");
    else { printf("FAIL: zero tile size accepted\n"); ++fails; }

    RScene s;
    build_scene(&s);
    RBox win;
    scene_bbox(&s, &win);
    RPlace place = {0.0, 0.0, 0.0, 1.0};
    const int sizes[3] = {1024, 2048, 4096};
    const int tile = argc > 1 ? atoi(argv[1]) : 16;

    printf("%-6s %10s %10s %9s %10s %12s\n", "size", "base ms", "cache ms", "hit %", "peak MB", "bytes");
    for (int t = 0; t < 3; ++t) {
        int w = sizes[t], h = sizes[t];
        unsigned char *ref = (unsigned char *)malloc((size_t)w * h);
        unsigned char *out = (unsigned char *)malloc((size_t)w * h);
        if (!ref || !out) { printf("FAIL: alloc\n"); return 1; }

        double t0 = now_ms();
        if (pipeline_render(&s, place, win, w, h, ref) != 0) { printf("FAIL: base render\n"); return 1; }
        double base_ms = now_ms() - t0;

        TileCache tc;
        if (tc_init(&tc, tile, tile) != 0) { printf("FAIL: tc_init\n"); return 1; }
        t0 = now_ms();
        int rc = pipeline_render_tc(&s, place, win, w, h, out, &tc);
        double cache_ms = now_ms() - t0;
        long peak = peak_kb();
        if (rc != 0) { printf("FAIL: cached render rc=%d\n", rc); ++fails; }
        if (tc.collisions != 0) {
            printf("FAIL: %ld cache key collisions at %d\n", tc.collisions, w);
            ++fails;
        }
        int same = memcmp(ref, out, (size_t)w * h) == 0;
        if (!same) { printf("FAIL: cached output differs at %d\n", w); ++fails; }
        long total = tc.hits + tc.misses;
        double hitpct = total ? 100.0 * tc.hits / total : 0.0;
        printf("%-6d %10.1f %10.1f %8.2f%% %10.1f %12s\n",
               w, base_ms, cache_ms, hitpct, peak / 1024.0, same ? "identical" : "DIFF");
        tc_free(&tc);
        free(ref);
        free(out);
    }
    printf(fails ? "BENCH FAIL %d\n" : "BENCH OK\n", fails);
    return fails ? 1 : 0;
}
