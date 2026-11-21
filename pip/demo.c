#define _POSIX_C_SOURCE 199309L
#include "pip.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* 参考实现：射线法奇偶规则，用于核对 100 点组 */
static int ref_contains(const PPoint *poly, int n, PPoint p) {
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        double yi = poly[i].y, yj = poly[j].y;
        if ((yi > p.y) != (yj > p.y)) {
            double x = poly[i].x + (p.y - yi) * (poly[j].x - poly[i].x) / (yj - yi);
            if (p.x < x) inside = !inside;
        }
    }
    return inside;
}

static unsigned rng_state = 12345u;
static double frand(double lo, double hi) {
    rng_state = rng_state * 1103515245u + 12345u;
    return lo + (hi - lo) * ((rng_state >> 8) & 0xFFFFFFu) / 16777216.0;
}

static double ms_between(struct timespec a, struct timespec b) {
    return (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6;
}

int main(void) {
    srand(7); /* 边上随机判定可复现 */
    FILE *fp = fopen("demo.txt", "w");
    if (!fp) return 1;
    int err_at = -1, rc;
    struct timespec t0, t1;

    /* 第一组：9 个手算点，逆时针正方形 */
    PPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    PPoint g1[9] = {{5,5},{15,5},{-5,5},{5,-5},{5,15},{0,5},{10,10},{2,2},{9,9}};
    int exp_w[9]  = {1,0,0,0,0,1,0,1,1};  /* 手算绕数 */
    int exp_in[9] = {1,0,0,0,0,-1,-1,1,1}; /* -1 = 边上随机 */
    PIPVerdict v1[9];
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = pip_batch(sq,4,g1,9,1,v1,&err_at);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (rc != 0) { printf("group1 error rc=%d at=%d\n", rc, err_at); fclose(fp); return 1; }
    int err1 = 0;
    for (int i = 0; i < 9; ++i) {
        if (v1[i].winding != exp_w[i]) ++err1;
        if (exp_in[i] >= 0 && v1[i].result != (exp_in[i] ? PIP_INSIDE : PIP_OUTSIDE)) ++err1;
    }
    double ms1 = ms_between(t0, t1);
    printf("group1 hand9: points=9 errors=%d elapsed=%.3fms\n", err1, ms1);
    fprintf(fp, "group1 hand9: points=9 errors=%d elapsed=%.3fms\n", err1, ms1);
    pip_table_csv(fp, g1, v1, 9);

    /* 第二组：100 个伪随机点，逆时针六边形，与射线法参考核对 */
    PPoint hex[6] = {{0,0},{8,0},{10,4},{6,9},{2,8},{-1,4}};
    PPoint g2[100];
    PIPVerdict v2[100];
    for (int i = 0; i < 100; ++i) g2[i].x = frand(-4.0, 14.0), g2[i].y = frand(-4.0, 14.0);
    clock_gettime(CLOCK_MONOTONIC, &t0);
    rc = pip_batch(hex,6,g2,100,1,v2,&err_at);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (rc != 0) { printf("group2 error rc=%d at=%d\n", rc, err_at); fclose(fp); return 1; }
    int err2 = 0;
    for (int i = 0; i < 100; ++i)
        if (!v2[i].on_edge && (v2[i].result == PIP_INSIDE) != ref_contains(hex,6,g2[i])) ++err2;
    double ms2 = ms_between(t0, t1);
    printf("group2 rand100: points=100 errors=%d elapsed=%.3fms\n", err2, ms2);
    fprintf(fp, "group2 rand100: points=100 errors=%d elapsed=%.3fms\n", err2, ms2);
    pip_table_csv(fp, g2, v2, 100);

    fclose(fp);
    printf("table exported to demo.txt\n");
    return 0;
}
