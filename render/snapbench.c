#define _POSIX_C_SOURCE 199309L
/* 快照验收基准：三组场景，各报快照字节数、还原差异像素、往返毫秒。
 * 直接渲染与「保存 -> 读回 -> 再渲染」两张 PGM 缓冲逐像素比对，
 * 差异像素非零即整体失败（退出码 1）。 */
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/export.h"
#include "src/snapshot.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define ITERS 100

typedef struct {
    const char *path;
    int w, h;
    RPlace place;
    RBox win;
} Case;

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int diff_pixels(const unsigned char *a, const unsigned char *b, int n) {
    int d = 0;
    for (int i = 0; i < n; ++i) if (a[i] != b[i]) ++d;
    return d;
}

static int scenes_equal(const RScene *a, const RScene *b) {
    if (a->npolys != b->npolys) return 0;
    for (int k = 0; k < a->npolys; ++k) {
        if (a->polys[k].n != b->polys[k].n) return 0;
        for (int i = 0; i < a->polys[k].n; ++i)
            if (a->polys[k].pts[i].x != b->polys[k].pts[i].x ||
                a->polys[k].pts[i].y != b->polys[k].pts[i].y)
                return 0;
    }
    return 1;
}

int main(int argc, char **argv) {
    const char *dir = argc > 1 ? argv[1] : "scenes";
    Case cases[3] = {
        {"/s1_square.txt", 64, 64, {0, 0, 0, 1.0}, {0, 0, 10, 10}},
        {"/s2_two.txt",   80, 80, {2.5, -1.0, 0.15, 1.2}, {0, 0, 20, 16}},
        {"/s3_three.txt", 96, 96, {-3.0, 4.0, 0.6, 0.8}, {-12, -10, 12, 12}},
    };
    char spath[256], snap[256];
    int failures = 0;
    printf("%-12s %10s %12s %14s\n", "scene", "bytes", "diff_pixels", "rt_ms(avg/100)");
    for (int c = 0; c < 3; ++c) {
        snprintf(spath, sizeof(spath), "%s%s", dir, cases[c].path);
        snprintf(snap, sizeof(snap), "build/bench_%d.snap", c);

        RScene s;
        if (scene_load_txt(spath, &s) <= 0) {
            printf("%-12s LOAD FAILED\n", cases[c].path);
            failures = 1;
            continue;
        }
        int npx = cases[c].w * cases[c].h;
        unsigned char *ref = malloc((size_t)npx), *out = malloc((size_t)npx);
        if (!ref || !out) return 2;
        if (pipeline_render(&s, cases[c].place, cases[c].win,
                            cases[c].w, cases[c].h, ref) != 0) {
            printf("%-12s RENDER FAILED\n", cases[c].path);
            failures = 1;
            continue;
        }

        long off;
        int rc = snapshot_save(snap, &s, cases[c].place, &cases[c].win,
                               cases[c].w, cases[c].h, &off);
        if (rc != SNAP_OK) {
            printf("%-12s SAVE FAILED: %s off=%ld\n",
                   cases[c].path, snapshot_strerror(rc), off);
            failures = 1;
            continue;
        }
        long bytes = snapshot_size_bytes(&s);

        /* 一次完整结构核对：读回的场景/变换/窗口必须逐字段一致。 */
        RScene s2;
        RPlace p2;
        RBox w2;
        int ww, hh;
        rc = snapshot_load(snap, &s2, &p2, &w2, &ww, &hh, &off);
        if (rc != SNAP_OK || !scenes_equal(&s, &s2) ||
            memcmp(&p2, &cases[c].place, sizeof(RPlace)) != 0 ||
            memcmp(&w2, &cases[c].win, sizeof(RBox)) != 0 ||
            ww != cases[c].w || hh != cases[c].h) {
            printf("%-12s STRUCT MISMATCH rc=%d off=%ld\n",
                   cases[c].path, rc, off);
            failures = 1;
            continue;
        }

        double t0 = now_ms();
        int diff = -1;
        for (int it = 0; it < ITERS; ++it) {
            RScene sr;
            RPlace pr;
            RBox wr;
            int wr_w, wr_h;
            rc = snapshot_load(snap, &sr, &pr, &wr, &wr_w, &wr_h, &off);
            if (rc != SNAP_OK) {
                printf("%-12s LOAD FAILED: %s off=%ld\n",
                       cases[c].path, snapshot_strerror(rc), off);
                failures = 1;
                break;
            }
            if (pipeline_render(&sr, pr, wr, wr_w, wr_h, out) != 0) {
                printf("%-12s RESTORE RENDER FAILED\n", cases[c].path);
                failures = 1;
                break;
            }
            diff = diff_pixels(ref, out, npx);
        }
        double elapsed = now_ms() - t0;
        double avg = elapsed / ITERS;
        const char *name = strrchr(cases[c].path, '/') + 1;
        printf("%-12s %10ld %12d %14.4f\n", name, bytes, diff, avg);
        if (diff != 0) failures = 1;
        free(ref);
        free(out);
    }
    printf(failures ? "SUMMARY: FAIL\n" : "SUMMARY: PASS (all diffs zero)\n");
    return failures;
}
