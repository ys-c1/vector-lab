/* 导出严格凸包顶点（共线/内点剔除后的 hull_convex 结果），
 * 供 gold/ref area 独立核算。用法：dump_hull <data.txt> [out.txt] */
#include "hull.h"
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc < 2) return 1;
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    HPoint pts[64];
    int n = 0;
    while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) ++n;
    fclose(f);
    HPoly hull;
    if (hull_convex(pts, n, &hull) != 0) { fprintf(stderr, "hull failed\n"); return 3; }
    FILE *o = argc >= 3 ? fopen(argv[2], "w") : stdout;
    if (!o) return 4;
    for (int i = 0; i < hull.count; ++i)
        fprintf(o, "%.6f %.6f\n", hull.pts[i].x, hull.pts[i].y);
    if (o != stdout) fclose(o);
    return 0;
}
