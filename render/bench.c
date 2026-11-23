/* 两后端基准与逐字节比对。
 * 每个 (画幅, 后端) 组合在独立子进程中运行，避免互相干扰内存峰值：
 *   - 毫秒：子进程内部 clock_gettime 计时，REPEAT 次取最小值，经管道回传
 *   - 内存峰值：wait4 取该子进程 rusage.ru_maxrss（Linux 单位 KB）
 *   - 差异字节：两后端 PGM 逐字节计数，非 0 即失败
 * 用法：bench scene.txt outdir w1 h1 w2 h2 w3 h3
 */
#define _POSIX_C_SOURCE 200809L
#define _DEFAULT_SOURCE
#include "src/scene.h"
#include "src/pipeline.h"
#include "src/backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <errno.h>

#define REPEAT 3
#define NBACKEND 2

static long long now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int load_scene(const char *path, RScene *s) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    scene_init(s);
    char tag[16];
    while (fscanf(f, "%15s", tag) == 1) {
        if (tag[0] != 'p') break;
        RPoint pts[64];
        int n = 0;
        while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) {
            ++n;
            int c = fgetc(f);
            if (c == '\n' || c == EOF) break;
            ungetc(c, f);
        }
        if (scene_add_poly(s, pts, n) != 0) { fclose(f); return -2; }
    }
    fclose(f);
    return s->npolys > 0 ? 0 : -3;
}

static long count_diff(const char *pa, const char *pb) {
    FILE *fa = fopen(pa, "rb"), *fb = fopen(pb, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return -1; }
    int wa, ha, wb, hb, maxv;
    if (fscanf(fa, "P5\n%d %d\n%d\n", &wa, &ha, &maxv) != 3 ||
        fscanf(fb, "P5\n%d %d\n%d\n", &wb, &hb, &maxv) != 3 ||
        wa != wb || ha != hb) { fclose(fa); fclose(fb); return -1; }
    long diff = 0;
    int ca, cb;
    while ((ca = fgetc(fa)) != EOF && (cb = fgetc(fb)) != EOF)
        if (ca != cb) ++diff;
    fclose(fa); fclose(fb);
    return diff;
}

/* 子进程：重复渲染 REPEAT 次取最小毫秒，末次写 PGM，毫秒经管道回传。 */
static int child_run(int fd, int backend, const char *scene, const char *out,
                     int w, int h, double scale) {
    RScene s;
    if (load_scene(scene, &s) != 0) return 20;
    RBox win;
    if (scene_bbox(&s, &win) != 0) return 21;
    unsigned char *buf = (unsigned char*)malloc((size_t)w * (size_t)h);
    if (!buf) return 22;
    RPlace place = {0.0, 0.0, 0.0, scale};
    long long best = 0;
    int rc = 0;
    for (int r = 0; r < REPEAT; ++r) {
        long long t0 = now_us();
        rc = pipeline_render_backend(backend, &s, place, win, w, h, buf);
        long long dt = now_us() - t0;
        if (rc != 0) break;
        if (r == 0 || dt < best) best = dt;
    }
    if (rc != 0) { free(buf); return 23; }
    if (write(fd, &best, sizeof best) != (ssize_t)sizeof best) { free(buf); return 26; }
    FILE *f = fopen(out, "wb");
    if (!f) { free(buf); return 24; }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    if (fwrite(buf, 1, (size_t)w * (size_t)h, f) != (size_t)w * (size_t)h) {
        fclose(f); free(buf); return 25;
    }
    fclose(f);
    free(buf);
    return 0;
}

struct Res { double ms_min; long maxrss_kb; int exit_code; };

static int run_one(int backend, const char *scene, const char *out,
                   int w, int h, double scale, struct Res *res) {
    int pfd[2];
    if (pipe(pfd) != 0) return -1;
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); return -1; }
    if (pid == 0) {
        close(pfd[0]);
        int c = child_run(pfd[1], backend, scene, out, w, h, scale);
        close(pfd[1]);
        _exit(c);
    }
    close(pfd[1]);
    long long us = -1;
    ssize_t got = 0, off = 0;
    char *p = (char*)&us;
    while (got < (ssize_t)sizeof us) {
        ssize_t n = read(pfd[0], p + off, sizeof us - (size_t)got);
        if (n <= 0) break;
        got += n; off += n;
    }
    close(pfd[0]);
    int status = 0;
    struct rusage ru;
    if (wait4(pid, &status, 0, &ru) < 0) return -1;
    res->ms_min = got == (ssize_t)sizeof us ? (double)us / 1000.0 : -1.0;
    res->maxrss_kb = ru.ru_maxrss;   /* Linux: KB */
    res->exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 9) {
        fprintf(stderr, "usage: %s scene.txt outdir w1 h1 w2 h2 w3 h3\n", argv[0]);
        return 1;
    }
    const char *scene = argv[1];
    const char *outdir = argv[2];
    int dims[3][2] = {
        {atoi(argv[3]), atoi(argv[4])},
        {atoi(argv[5]), atoi(argv[6])},
        {atoi(argv[7]), atoi(argv[8])}
    };
    RScene probe;
    if (load_scene(scene, &probe) != 0) {
        fprintf(stderr, "load scene failed: %s\n", scene);
        return 2;
    }

    printf("%-10s | %-16s | %12s | %12s | %10s\n",
           "frame", "backend", "ms(min of3)", "peak_rss_kb", "diff_bytes");
    printf("-----------+------------------+--------------+--------------+-----------\n");

    int bad = 0;
    for (int d = 0; d < 3; ++d) {
        int w = dims[d][0], h = dims[d][1];
        char o1[256], o2[256];
        snprintf(o1, sizeof o1, "%s/bench_%dx%d_b1.pgm", outdir, w, h);
        snprintf(o2, sizeof o2, "%s/bench_%dx%d_b2.pgm", outdir, w, h);
        struct Res r[NBACKEND];
        for (int be = 0; be < NBACKEND; ++be) {
            int backend = be + 1;
            const char *out = be == 0 ? o1 : o2;
            if (run_one(backend, scene, out, w, h, 1.0, &r[be]) != 0) {
                fprintf(stderr, "run failed w=%d h=%d backend=%d errno=%d\n",
                        w, h, backend, errno);
                return 3;
            }
            if (r[be].exit_code != 0) {
                fprintf(stderr, "child abnormal w=%d h=%d backend=%d code=%d\n",
                        w, h, backend, r[be].exit_code);
                bad = 1;
            }
        }
        long diff = count_diff(o1, o2);
        if (diff < 0) { fprintf(stderr, "diff failed %s %s\n", o1, o2); bad = 1; }
        if (diff != 0) bad = 1;
        char label[32];
        snprintf(label, sizeof label, "%dx%d", w, h);
        printf("%-10s | %-16s | %12.3f | %12ld | %10ld\n",
               label, "1 scanline", r[0].ms_min, r[0].maxrss_kb, diff);
        printf("%-10s | %-16s | %12.3f | %12ld | %10s\n",
               label, "2 tile-track", r[1].ms_min, r[1].maxrss_kb, "");
    }
    if (bad) {
        printf("RESULT: MISMATCH (byte-identical contract broken)\n");
        return 4;
    }
    printf("RESULT: byte-identical on all frames\n");
    return 0;
}
