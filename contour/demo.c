#define _DEFAULT_SOURCE
#include "contour.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* demo: 解析多层轮廓 -> 校验 -> 每层顶点+面积写同一个文件 ->
 *       逐层用 gold/ref 数格子量面积，核对容差与单调不增。
 * 用法: demo [input.txt] [ref 可执行] [out.txt]
 */
#define REL_TOL 0.05 /* gold 网格近似相对容差 5% */

static int run_gold(const char *ref, const char *poly, double *out) {
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s area %s", ref, poly);
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int rc = fscanf(fp, "%lf", out);
    int w = pclose(fp);
    if (rc != 1) return -1;
    return w == 0 ? 0 : -1;
}

int main(int argc, char **argv) {
    const char *in = argc > 1 ? argv[1] : "data/sample.txt";
    const char *ref = argc > 2 ? argv[2] : "../gold/build/ref";
    const char *out = argc > 3 ? argv[3] : "layers.txt";

    CTSet set;
    CTError err;
    int rc = contour_load(in, &set, &err);
    if (rc != CT_OK) {
        if (err.layer >= 0)
            fprintf(stderr, "REJECT layer %d: %s\n", err.layer, ct_strerror(err.code));
        else
            fprintf(stderr, "REJECT: %s\n", ct_strerror(err.code));
        return 1;
    }
    rc = contour_validate(&set, &err);
    if (rc != CT_OK) {
        fprintf(stderr, "REJECT layer %d: %s\n", err.layer, ct_strerror(err.code));
        contour_free(&set);
        return 1;
    }

    double *areas = (double*)malloc(sizeof(double) * (size_t)set.layers);
    double *gold = (double*)malloc(sizeof(double) * (size_t)set.layers);
    if (!areas || !gold) { free(areas); free(gold); contour_free(&set); return 2; }
    for (int l = 0; l < set.layers; ++l)
        areas[l] = contour_area(set.cs[l].pts, set.cs[l].count);

    CTError merr;
    if (contour_check_mono(&set, areas, &merr) != CT_OK) {
        fprintf(stderr, "REJECT layer %d: %s (shoelace area increases)\n",
                merr.layer, ct_strerror(merr.code));
        free(areas); free(gold); contour_free(&set);
        return 1;
    }

    /* 临时目录：每层一个 poly 文件喂给独立参照实现。*/
    char tdir[] = "/tmp/contour-demo-XXXXXX";
    if (!mkdtemp(tdir)) { perror("mkdtemp"); return 2; }
    struct stat st;
    int have_gold = stat(ref, &st) == 0;
    if (!have_gold)
        fprintf(stderr, "GOLD MISSING: %s not built (make -C ../gold)\n", ref);

    FILE *o = fopen(out, "w");
    if (!o) { perror(out); return 2; }
    long total = 0;
    fprintf(o, "# contour layers: %d\n", set.layers);
    for (int l = 0; l < set.layers; ++l) {
        const CTContour *c = &set.cs[l];
        total += c->count;
        fprintf(o, "L %d\nverts %d\n", c->layer, c->count);
        for (int i = 0; i < c->count; ++i)
            fprintf(o, "%.6f %.6f\n", c->pts[i].x, c->pts[i].y);
        fprintf(o, "area %.6f\n", areas[l]);

        gold[l] = -1.0;
        if (have_gold) {
            char poly[256];
            snprintf(poly, sizeof(poly), "%s/l%d.txt", tdir, c->layer);
            FILE *pf = fopen(poly, "w");
            if (!pf) { perror(poly); return 2; }
            for (int i = 0; i < c->count; ++i)
                fprintf(pf, "%.6f %.6f\n", c->pts[i].x, c->pts[i].y);
            fclose(pf);
            if (run_gold(ref, poly, &gold[l]) != 0) {
                fprintf(stderr, "REJECT layer %d: gold/ref failed\n", c->layer);
                return 1;
            }
            fprintf(o, "gold %.6f\n", gold[l]);
        }
    }
    fclose(o);

    int gold_fail = 0;
    double gmax = 0.0;
    for (int l = 0; l < set.layers; ++l) if (gold[l] > gmax) gmax = gold[l];
    double geps = gmax * 1e-3; /* 数格子噪声带 0.1% */
    if (have_gold) {
        for (int l = 0; l < set.layers; ++l) {
            double base = areas[l] > 1e-12 ? areas[l] : 1.0;
            double rel = (gold[l] - areas[l]) / base;
            printf("L %d verts %d area %.6f gold %.6f rel %+.4f\n",
                   set.cs[l].layer, set.cs[l].count, areas[l], gold[l], rel);
            if ((rel > REL_TOL || rel < -REL_TOL) && !gold_fail) {
                fprintf(stderr, "REJECT layer %d: gold area %.6f vs shoelace %.6f "
                        "(rel %.2f > %.0f%%)\n",
                        set.cs[l].layer, gold[l], areas[l], rel, REL_TOL * 100.0);
                gold_fail = 1;
            }
            if (l > 0 && gold[l] > gold[l-1] + geps) {
                fprintf(stderr, "REJECT layer %d: gold area not non-increasing "
                        "(%.6f > %.6f)\n", set.cs[l].layer, gold[l], gold[l-1]);
                gold_fail = 1;
            }
        }
    }

    printf("layers %d total_verts %ld mono ok mem %ld/%ld = %.2fx (limit 6x)\n",
           set.layers, total, set.peak_bytes, set.input_bytes,
           set.input_bytes ? (double)set.peak_bytes / (double)set.input_bytes : 0.0);
    printf("wrote %s\n", out);
    free(areas); free(gold);
    contour_free(&set);
    return (!have_gold || gold_fail) ? 1 : 0;
}
