/* sweep 基准与验证驱动：
 *   bench table  <file> --stable [--seed N]          交点表（事件处理顺序）
 *   bench events <file> --stable [--seed N]          排序后的事件与完整排序键
 *   bench ties   <file>                              键前缀相同但段不同的事件分组
 *   bench rerun  <file> --stable [--seed N] [--runs N]
 *       多次重跑，报告输出一致次数、不同顺序数、各次耗时
 *   bench canon  <file>                              规范化（按坐标/段对排序）交点表
 *
 * 输入格式：每行 "x1 y1 x2 y2 [id]"，# 开头为注释，空行忽略。
 * 输出统一 6 位小数，保证逐字节可比。
 */
#define _POSIX_C_SOURCE 199309L
#include "sweep.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAXN 4096

static const char *kind_name(SEventKind k) {
    return k == SEV_LEFT ? "LEFT " : k == SEV_CROSS ? "CROSS" : "RIGHT";
}

static int load_segs(const char *path, SSeg *out, int maxn) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); return -1; }
    int n = 0;
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        if (n >= maxn) { fprintf(stderr, "%s: too many segments\n", path); return -1; }
        double x1, y1, x2, y2;
        int id = n;
        int got = sscanf(line, "%lf %lf %lf %lf %d", &x1, &y1, &x2, &y2, &id);
        if (got < 4) { fprintf(stderr, "%s: bad line: %s", path, line); return -1; }
        out[n++] = (SSeg){ { x1, y1 }, { x2, y2 }, id };
    }
    fclose(f);
    return n;
}

static void print_slope(const SSeg *segs, int n, int seg_id) {
    for (int i = 0; i < n; ++i) {
        if (segs[i].id != seg_id) continue;
        double dx = segs[i].q.x - segs[i].p.x, dy = segs[i].q.y - segs[i].p.y;
        if (dx == 0.0) printf("slope=inf dir=(0,%.3g)", dy);
        else printf("slope=%.6f dir=(%.3g,%.3g)", dy / dx, dx, dy);
        return;
    }
    printf("slope=?");
}

static int cmd_table(const SSeg *segs, int n, int stable, long seed) {
    STable t; SError err;
    if (sweep_table(segs, n, stable, seed, &t, &err) != 0) {
        fprintf(stderr, "REJECT %s\n", sweep_strerror(&err));
        return 2;
    }
    printf("table mode=%s seed=%ld hits=%d\n",
           stable ? "stable" : "legacy", stable ? -1L : seed, t.count);
    for (int i = 0; i < t.count; ++i)
        printf("row %3d x=%9.6f y=%9.6f a=%d b=%d\n",
               i, t.hits[i].at.x, t.hits[i].at.y, t.hits[i].seg_a, t.hits[i].seg_b);
    sweep_free_table(&t);
    return 0;
}

static int cmd_events(const SSeg *segs, int n, int stable, long seed) {
    SEventList el; SError err;
    if (sweep_events(segs, n, stable, seed, &el, &err) != 0) {
        fprintf(stderr, "REJECT %s\n", sweep_strerror(&err));
        return 2;
    }
    printf("events mode=%s seed=%ld count=%d\n",
           stable ? "stable" : "legacy", stable ? -1L : seed, el.count);
    for (int i = 0; i < el.count; ++i) {
        const SEvent *e = &el.events[i];
        printf("ev %3d %s x=%9.6f y=%9.6f seg=%d pair=(%d,%d) ",
               i, kind_name(e->kind), e->x, e->y, e->seg_id, e->seg_a, e->seg_b);
        print_slope(segs, n, e->seg_id);
        putchar('\n');
    }
    sweep_free_events(&el);
    return 0;
}

/* 找出 (x,y,kind,斜率序) 相同但 seg_id 不同的事件：
 * 这正是“键相同但段不同、必须额外比较”的情形。 */
typedef struct { double dx, dy; } DVec;

static int slope_eq(const SSeg *segs, int n, int id1, int id2) {
    DVec v[2]; int k = 0;
    int ids[2] = { id1, id2 };
    for (int t = 0; t < 2; ++t) {
        for (int i = 0; i < n; ++i)
            if (segs[i].id == ids[t]) {
                double dx = segs[i].q.x - segs[i].p.x, dy = segs[i].q.y - segs[i].p.y;
                v[k++] = (DVec){ dx, dy };
            }
    }
    if (k != 2) return 0;
    /* 与 sweep.c 一致：竖直段斜率相等；否则叉积为 0 */
    if (v[0].dx == 0.0 && v[1].dx == 0.0) return 1;
    if (v[0].dx == 0.0 || v[1].dx == 0.0) return 0;
    return v[0].dx * v[1].dy - v[0].dy * v[1].dx == 0.0;
}

static int cmd_ties(const SSeg *segs, int n) {
    SEventList el; SError err;
    if (sweep_events(segs, n, 1, 0, &el, &err) != 0) {
        fprintf(stderr, "REJECT %s\n", sweep_strerror(&err));
        return 2;
    }
    int groups = 0;
    printf("tie-groups: same (x,y,kind,slope) but different seg_id\n");
    /* 事件已按全序排好，同 (x,y,kind) 的事件连续成块；块内再找同斜率不同段 */
    int i = 0;
    while (i < el.count) {
        int j = i + 1;
        while (j < el.count &&
               el.events[j].x == el.events[i].x &&
               el.events[j].y == el.events[i].y &&
               el.events[j].kind == el.events[i].kind)
            ++j;
        /* [i,j) 为同位置同类型块，两两检查斜率相同而段号不同 */
        int first = 1;
        for (int a = i; a < j; ++a)
            for (int b = a + 1; b < j; ++b) {
                const SEvent *ea = &el.events[a], *eb = &el.events[b];
                if (ea->seg_id == eb->seg_id) continue;
                if (!slope_eq(segs, n, ea->seg_id, eb->seg_id)) continue;
                if (first) {
                    ++groups;
                    printf("group at (%g,%g) %s:\n", ea->x, ea->y, kind_name(ea->kind));
                    first = 0;
                }
                printf("  seg %d vs seg %d -> full-key tie, broken by seg_id (%d<%d)\n",
                       ea->seg_id, eb->seg_id, ea->seg_id, eb->seg_id);
            }
        i = j;
    }
    if (!groups) printf("(none)\n");
    sweep_free_events(&el);
    return 0;
}

static unsigned long fnv(const char *s, size_t len) {
    unsigned long h = 1469598103934665603UL;
    for (size_t k = 0; k < len; ++k) { h ^= (unsigned char)s[k]; h *= 1099511628211UL; }
    return h;
}

static int cmd_rerun(const SSeg *segs, int n, int stable, long seed, int runs) {
    if (runs < 1) runs = 1;
    if (runs > 256) runs = 256;
    int distinct = 0;
    unsigned long first_hash = 0;
    unsigned long hashes[256];
    double ms[256];
    for (int r = 0; r < runs; ++r) {
        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);
        STable t; SError err;
        if (sweep_table(segs, n, stable, seed, &t, &err) != 0) {
            fprintf(stderr, "REJECT %s\n", sweep_strerror(&err));
            return 2;
        }
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms[r] = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
        char buf[65536];
        size_t off = 0;
        for (int i = 0; i < t.count; ++i)
            off += (size_t)snprintf(buf + off, sizeof(buf) - off,
                                    "%.6f %.6f %d %d\n",
                                    t.hits[i].at.x, t.hits[i].at.y,
                                    t.hits[i].seg_a, t.hits[i].seg_b);
        hashes[r] = fnv(buf, off);
        if (r == 0) first_hash = hashes[0];
        else {
            int seen = 0;
            for (int q = 0; q < r; ++q) if (hashes[q] == hashes[r]) { seen = 1; break; }
            if (!seen) ++distinct;
        }
        sweep_free_table(&t);
    }
    int consistent = 0;
    for (int r = 0; r < runs; ++r) if (hashes[r] == first_hash) ++consistent;
    double total = 0.0, best = ms[0], worst = ms[0];
    for (int r = 0; r < runs; ++r) {
        total += ms[r];
        if (ms[r] < best) best = ms[r];
        if (ms[r] > worst) worst = ms[r];
    }
    printf("rerun mode=%s runs=%d consistent=%d distinct_orders=%d "
           "avg_ms=%.4f min_ms=%.4f max_ms=%.4f\n",
           stable ? "stable" : "legacy", runs, consistent, distinct + 1,
           total / runs, best, worst);
    return 0;
}

static int canon_cmp(const void *pa, const void *pb) {
    const SHit *a = (const SHit *)pa, *b = (const SHit *)pb;
    if (a->at.x < b->at.x) return -1;
    if (a->at.x > b->at.x) return 1;
    if (a->at.y < b->at.y) return -1;
    if (a->at.y > b->at.y) return 1;
    if (a->seg_a != b->seg_a) return a->seg_a < b->seg_a ? -1 : 1;
    if (a->seg_b != b->seg_b) return a->seg_b < b->seg_b ? -1 : 1;
    return 0;
}

static int cmd_canon(const SSeg *segs, int n) {
    STable t; SError err;
    if (sweep_table(segs, n, 1, 0, &t, &err) != 0) {
        fprintf(stderr, "REJECT %s\n", sweep_strerror(&err));
        return 2;
    }
    qsort(t.hits, (size_t)t.count, sizeof(SHit), canon_cmp);
    printf("canon hits=%d\n", t.count);
    for (int i = 0; i < t.count; ++i)
        printf("%.6f %.6f %d %d\n",
               t.hits[i].at.x, t.hits[i].at.y,
               t.hits[i].seg_a, t.hits[i].seg_b);
    sweep_free_table(&t);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s <table|events|ties|rerun|canon> <file> "
                "[--stable] [--seed N] [--runs N]\n", argv[0]);
        return 1;
    }
    const char *cmd = argv[1], *path = argv[2];
    int stable = 0, runs = 10;
    long seed = 1234;
    for (int i = 3; i < argc; ++i) {
        if (!strcmp(argv[i], "--stable")) stable = 1;
        else if (!strcmp(argv[i], "--seed") && i + 1 < argc) seed = atol(argv[++i]);
        else if (!strcmp(argv[i], "--runs") && i + 1 < argc) runs = atoi(argv[++i]);
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 1; }
    }
    static SSeg segs[MAXN];
    int n = load_segs(path, segs, MAXN);
    if (n < 0) return 1;
    if (!strcmp(cmd, "table"))  return cmd_table(segs, n, stable, seed);
    if (!strcmp(cmd, "events")) return cmd_events(segs, n, stable, seed);
    if (!strcmp(cmd, "ties"))   return cmd_ties(segs, n);
    if (!strcmp(cmd, "rerun"))  return cmd_rerun(segs, n, stable, seed, runs);
    if (!strcmp(cmd, "canon"))  return cmd_canon(segs, n);
    fprintf(stderr, "unknown command %s\n", cmd);
    return 1;
}
