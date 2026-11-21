#include "clip.h"
#include <stdio.h>
#include <math.h>
#include <time.h>

#define PI 3.14159265358979323846

static void print_offset_table(const CPoly *ring, double dist) {
    CLine lines[CLIP_MAX];
    if (clip_offset_lines(ring, dist, lines, CLIP_MAX) != 0) {
        printf("offset table: error\n");
        return;
    }
    printf("offset table (dist=%.3f), %d edges:\n", dist, ring->count);
    printf(" edge | inward normal (nx, ny)  | offset line a*x + b*y + c = 0\n");
    for (int i = 0; i < ring->count; ++i)
        printf(" %4d | (% .6f, % .6f)     | % .6f*x %+.6f*y %+.6f = 0\n",
               i, lines[i].a, lines[i].b, lines[i].a, lines[i].b, lines[i].c);
}

static void bench(int n, double R, double dist, int reps) {
    CPoly in, out;
    in.count = n;
    for (int i = 0; i < n; ++i) {
        double t = 2.0 * PI * i / n;
        in.pts[i].x = 50.0 + R * cos(t);
        in.pts[i].y = 50.0 + R * sin(t);
    }
    /* 正 n 边形手算解析解：收缩后Apothem = R*cos(pi/n) - dist */
    double ap = R * cos(PI / n) - dist;
    double exact = n * ap * ap * tan(PI / n);

    int rc = 0, ee = -1;
    clock_t t0 = clock();
    for (int r = 0; r < reps; ++r) rc = clip_shrink(&in, dist, &out, &ee);
    double us = 1e6 * (double)(clock() - t0) / CLOCKS_PER_SEC / reps;

    double area = clip_area(&out);
    printf("bench n=%3d dist=%.2f: rc=%d verts=%d area=%.6f exact=%.6f "
           "area_err=%.3e (rel %.3e)  %.2f us/call (%d reps)\n",
           n, dist, rc, out.count, area, exact,
           fabs(area - exact), fabs(area - exact) / exact, us, reps);
}

int main(void) {
    CPoint poly[4] = {{0,0},{20,0},{20,10},{0,10}};
    CPoint win[4] = {{5,-5},{15,-5},{15,5},{5,5}};
    CPoly clipped, off0, shrunk;
    if (clip_convex(poly,4,win,4,&clipped) != 0) return 1;
    printf("clipped %d area %.2f\n", clipped.count, clip_area(&clipped));

    /* 偏移关掉：结果与当前版本一致 */
    if (clip_shrink(&clipped, 0.0, &off0, NULL) != 0) return 1;
    printf("shrink off  %d area %.2f (identical to clipped)\n",
           off0.count, clip_area(&off0));

    /* 裁剪完再按距离收缩 */
    int ee = -1;
    int rc = clip_shrink(&clipped, 1.0, &shrunk, &ee);
    if (rc != 0) { printf("shrink failed rc=%d edge=%d\n", rc, ee); return 1; }
    printf("shrunk 1.0  %d area %.2f\n", shrunk.count, clip_area(&shrunk));

    /* 偏移表按边号列出 */
    print_offset_table(&clipped, 1.0);
    printf("\n");

    /* 32 与 128 顶点两档：面积误差、顶点数、耗时 */
    bench(32, 40.0, 5.0, 20000);
    bench(128, 40.0, 5.0, 5000);
    return 0;
}
