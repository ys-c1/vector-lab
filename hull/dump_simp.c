/* 导出精简后的多边形顶点（每行 "x y"），供 gold/ref area 独立核算。
 * 用法：dump_simp <data.txt> <ratio> [out.txt] */
#include "simplify.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    FILE *f = fopen(argv[1], "r");
    if (!f) return 2;
    HPoint pts[64];
    int n = 0;
    while (n < 64 && fscanf(f, "%lf %lf", &pts[n].x, &pts[n].y) == 2) ++n;
    fclose(f);
    HSimp r;
    int rc = hull_simplify(pts, n, atof(argv[2]), &r);
    if (rc != 0) { fprintf(stderr, "rejected rc=%d\n", rc); return 3; }
    FILE *o = argc >= 4 ? fopen(argv[3], "w") : stdout;
    if (!o) return 4;
    for (int i = 0; i < r.kept; ++i)
        fprintf(o, "%.6f %.6f\n", r.poly.pts[i].x, r.poly.pts[i].y);
    if (o != stdout) fclose(o);
    return 0;
}
