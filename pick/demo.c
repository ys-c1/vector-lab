#define _POSIX_C_SOURCE 199309L
#include "pick.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static unsigned long long rng_state = 20261122ULL;
static double frand(void) {
    rng_state = rng_state * 6364136223846793005ULL + 1442695040888963407ULL;
    return (double)(rng_state >> 11) * (1.0 / 9007199254740992.0);
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

/* 逐段手算的最近命中距离，作为独立核对（d 已归一化，t 即距离） */
static double naive_nearest_dist(const PSeg *segs, int n, PPoint o, PPoint d) {
    double best = -1.0;
    for (int i = 0; i < n; ++i) {
        double sx = segs[i].p1.x - segs[i].p0.x, sy = segs[i].p1.y - segs[i].p0.y;
        double den = d.x * sy - d.y * sx;
        if (fabs(den) < 1e-12) continue;
        double qx = segs[i].p0.x - o.x, qy = segs[i].p0.y - o.y;
        double t = (qx * sy - qy * sx) / den;
        double u = (qx * d.y - qy * d.x) / den;
        if (t < 0.0 || u < 0.0 || u > 1.0) continue;
        double dist = hypot(d.x * t, d.y * t);
        if (best < 0.0 || dist < best) best = dist;
    }
    return best;
}

static void run_tier(int n, int rays, FILE *rep, const char *csv_path) {
    PSeg *segs = malloc(sizeof(PSeg) * (size_t)n);
    for (int i = 0; i < n; ++i) {
        double cx = frand() * 1000.0, cy = frand() * 1000.0;
        double a = frand() * 6.283185307179586, half = 2.0 + 18.0 * frand();
        segs[i].p0.x = cx - half * cos(a); segs[i].p0.y = cy - half * sin(a);
        segs[i].p1.x = cx + half * cos(a); segs[i].p1.y = cy + half * sin(a);
    }
    PIndex idx;
    int err_seg = -1;
    if (pick_index_build(segs, n, &idx, &err_seg) != PICK_OK) {
        fprintf(stderr, "build failed, seg %d\n", err_seg);
        free(segs);
        exit(1);
    }

    PPoint *ro = malloc(sizeof(PPoint) * (size_t)rays);
    PPoint *rd = malloc(sizeof(PPoint) * (size_t)rays);
    for (int r = 0; r < rays; ++r) {
        ro[r].x = frand() * 1000.0; ro[r].y = frand() * 1000.0;
        double a = frand() * 6.283185307179586;
        rd[r].x = cos(a); rd[r].y = sin(a); /* 单位方向，t 即命中距离 */
    }

    PHit *table = malloc(sizeof(PHit) * (size_t)rays);
    int ntable = 0;
    long visited_on = 0, visited_off = 0;
    int mismatches = 0;
    double max_err = 0.0, ms_on, ms_off;
    PStats st;

    double t0 = now_ms();
    for (int r = 0; r < rays; ++r) {
        PHit h;
        pick_ray_nearest(&idx, ro[r], rd[r], 1, &h, &st);
        visited_on += st.segs_tested;
        if (h.seg >= 0) table[ntable++] = h;
        else { PHit miss = {-1, 0, 0, {0, 0}}; table[ntable++] = miss; }
    }
    ms_on = now_ms() - t0;

    t0 = now_ms();
    for (int r = 0; r < rays; ++r) {
        PHit h;
        pick_ray_nearest(&idx, ro[r], rd[r], 0, &h, &st);
        visited_off += st.segs_tested;
        /* 与剔除开启的结果及手算距离核对 */
        if (h.seg != table[r].seg || h.t != table[r].t) ++mismatches;
        double ref = naive_nearest_dist(segs, n, ro[r], rd[r]);
        double err = (h.seg < 0) ? (ref < 0.0 ? 0.0 : 1e30) : fabs(h.t - ref);
        if (err > max_err) max_err = err;
    }
    ms_off = now_ms() - t0;

    fprintf(rep, "segments=%d rays=%d\n", n, rays);
    fprintf(rep, "  cull on : visited %ld segs (avg %.1f/ray), %.3f ms\n",
            visited_on, (double)visited_on / rays, ms_on);
    fprintf(rep, "  cull off: visited %ld segs (avg %.1f/ray), %.3f ms\n",
            visited_off, (double)visited_off / rays, ms_off);
    fprintf(rep, "  max hit-distance error vs naive: %.3e\n", max_err);
    fprintf(rep, "  hit mismatches (cull on vs off): %d\n", mismatches);

    if (csv_path) {
        int kept = 0;
        for (int r = 0; r < ntable; ++r)
            if (table[r].seg >= 0) table[kept++] = table[r];
        if (pick_hits_export(table, kept, csv_path) == PICK_OK)
            fprintf(rep, "  exported %s (%d hit rows)\n", csv_path, kept);
        else
            fprintf(rep, "  export %s failed\n", csv_path);
    }

    free(table); free(rd); free(ro);
    pick_index_free(&idx);
    free(segs);
}

int main(void) {
    FILE *rep = fopen("demo.txt", "w");
    if (!rep) rep = stdout;
    run_tier(1000, 300, rep, 0);
    run_tier(10000, 300, rep, "hits.csv");
    if (rep != stdout) fclose(rep);
    /* 摘要回显到终端 */
    FILE *f = fopen("demo.txt", "r");
    if (f) {
        char buf[256];
        while (fgets(buf, sizeof buf, f)) fputs(buf, stdout);
        fclose(f);
    }
    return 0;
}
