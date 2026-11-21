#define _POSIX_C_SOURCE 199309L
#include "stroke.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double ring_area(const SOutline *p) {
    double s = 0.0;
    for (int i = 0; i < p->count; ++i) {
        SPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        s += a.x * b.y - b.x * a.y;
    }
    return fabs(s) * 0.5;
}

static const char *jname(int j) {
    static const char *n[5] = {"auto", "miter", "bevel", "round", "legacy"};
    return n[j];
}

/* 生成 n_corner 个拐点的星形折线（半径交替，保证各种夹角都出现） */
static int make_path(int n_corner, SPoint *pts, int cap) {
    int n = n_corner + 2;
    if (n > cap) return -1;
    pts[0].x = 0.0; pts[0].y = -20.0;
    for (int i = 0; i <= n_corner; ++i) {
        double ang = -M_PI * 0.5 + (double)i / n_corner * M_PI * 1.5;
        double r = (i % 2) ? 18.0 : 30.0;
        pts[i + 1].x = r * cos(ang);
        pts[i + 1].y = r * sin(ang);
    }
    return n;
}

int main(int argc, char **argv) {
    const char *csv = argc > 1 ? argv[1] : "bench/selection.csv";
    const int REP = 2000;
    int groups[2] = {8, 64};
    static SPoint pts[STROKE_MAX_IN + 1];
    SOutline out;

    FILE *f = fopen(csv, "w");
    if (!f) { perror(csv); return 1; }
    fprintf(f, "corners,corner_index,angle_deg,chosen_join,miter_ratio,rejected,reason\n");

    printf("corners  join    points   area            time_ms(avg of %d)\n", REP);
    for (int g = 0; g < 2; ++g) {
        int n = make_path(groups[g], pts, STROKE_MAX_IN + 1);
        int modes[4] = { SJ_AUTO, SJ_MITER, SJ_BEVEL, SJ_ROUND };
        for (int mi = 0; mi < 4; ++mi) {
            SStrokeOpt opt = { .join = modes[mi], .miter_limit = 4.0 };
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            int rc = 0, bad = -1;
            for (int r = 0; r < REP; ++r)
                rc = stroke_outline(pts, n, 2.0, &opt, &out, &bad);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            if (rc != 0) {
                printf("%7d  %-6s REJECTED rc=%d corner=%d\n",
                       groups[g], jname(modes[mi]), rc, bad);
                continue;
            }
            double ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
            printf("%7d  %-6s %7d  %14.6f  %10.4f\n",
                   groups[g], jname(modes[mi]), out.count, ring_area(&out), ms / REP);

            if (modes[mi] == SJ_AUTO)
                for (int i = 0; i < opt.record_count; ++i) {
                    SJoinInfo *r = &opt.info[i];
                    fprintf(f, "%d,%d,%.6f,%s,%.6f,%d,%d\n",
                            groups[g], r->index, r->angle * 180.0 / M_PI,
                            jname(r->join), r->miter_ratio, r->rejected, r->reason);
                }
        }
    }
    fclose(f);
    printf("selection table written to %s\n", csv);
    return 0;
}
