/* 逐点比对两个点文件，报告坐标差异上界。
 * 用法：ptdiff <a.txt> <b.txt> <tol>
 * 点文件格式：首行点数 n，随后 n 行 "x y"。
 * 输出：points=<n> max_dx=<..> max_dy=<..> bound=<tol> PASS|FAIL
 * 行数不符：拒绝并指名参数 count，退出码 2。
 */
#include <stdio.h>
#include <stdlib.h>

static int load_hdr(FILE *f, const char *path, size_t *n) {
    if (!f) { perror(path); return -1; }
    if (fscanf(f, "%zu", n) != 1) {
        fprintf(stderr, "reject: bad point file (parameter: count) %s\n", path);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: ptdiff <a.txt> <b.txt> <tol>\n");
        return 1;
    }
    FILE *fa = fopen(argv[1], "r"), *fb = fopen(argv[2], "r");
    size_t na = 0, nb = 0;
    if (load_hdr(fa, argv[1], &na) || load_hdr(fb, argv[2], &nb)) return 2;
    if (na != nb) {
        fprintf(stderr, "reject: length mismatch (parameter: count) %s=%zu %s=%zu\n",
                argv[1], na, argv[2], nb);
        return 2;
    }
    double tol = strtod(argv[3], 0);
    double max_dx = 0.0, max_dy = 0.0;
    size_t i = 0;
    for (; i < na; ++i) {
        double xa, ya, xb, yb;
        if (fscanf(fa, "%lf %lf", &xa, &ya) != 2 ||
            fscanf(fb, "%lf %lf", &xb, &yb) != 2) break;
        double dx = xa - xb, dy = ya - yb;
        if (dx < 0) dx = -dx;
        if (dy < 0) dy = -dy;
        if (dx > max_dx) max_dx = dx;
        if (dy > max_dy) max_dy = dy;
    }
    fclose(fa);
    fclose(fb);
    if (i != na) {
        fprintf(stderr, "reject: length mismatch (parameter: count) header=%zu actual=%zu\n", na, i);
        return 2;
    }
    int ok = max_dx <= tol && max_dy <= tol;
    printf("points=%zu max_dx=%.3g max_dy=%.3g bound=%.3g %s\n",
           na, max_dx, max_dy, tol, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
