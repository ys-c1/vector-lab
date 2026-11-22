/* 逐点比对两个点文件：点数必须一致，逐点求偏差，累计偏差不许超容差。
 * 用法: cmp_points <a.txt> <b.txt> [tol]     默认 tol = 1e-6
 * 文件格式: 首行点数，随后每行 "x y"。
 * 输出: 点数、最大单点偏差、累计偏差；超限退出码 1。
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int load(const char *path, double **out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n;
    if (fscanf(f, "%d", &n) != 1 || n < 0) { fclose(f); return -1; }
    double *p = (double *)malloc((size_t)(n > 0 ? n : 1) * 2 * sizeof(double));
    if (!p) { fclose(f); return -1; }
    for (int i = 0; i < n; ++i)
        if (fscanf(f, "%lf %lf", &p[2*i], &p[2*i+1]) != 2) { free(p); fclose(f); return -1; }
    fclose(f);
    *out = p;
    return n;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: cmp_points <a.txt> <b.txt> [tol]\n");
        return 2;
    }
    double tol = argc > 3 ? atof(argv[3]) : 1e-6;
    double *a = NULL, *b = NULL;
    int na = load(argv[1], &a), nb = load(argv[2], &b);
    if (na < 0 || nb < 0) {
        fprintf(stderr, "FAIL cannot load %s %s\n", argv[1], argv[2]);
        return 2;
    }
    if (na != nb) {
        printf("FAIL count %d != %d\n", na, nb);
        free(a); free(b);
        return 1;
    }
    double maxd = 0.0, cum = 0.0;
    for (int i = 0; i < na; ++i) {
        double dx = a[2*i] - b[2*i], dy = a[2*i+1] - b[2*i+1];
        double d = sqrt(dx * dx + dy * dy);
        if (d > maxd) maxd = d;
        cum += d;
    }
    int ok = cum <= tol;
    printf("%s points %d max %.9g cum %.9g tol %.9g\n",
           ok ? "OK" : "FAIL", na, maxd, cum, tol);
    free(a); free(b);
    return ok ? 0 : 1;
}
