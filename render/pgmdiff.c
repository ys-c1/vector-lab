/* 逐像素比对两张 PGM（P5, 8-bit）。报告差异像素数与首个差异坐标。
 * 用于验证四环境产物彼此一致、且与基准图一致。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_pgm(const char *path, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char magic[3] = {0};
    int maxv = 0;
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P5") != 0) { fclose(f); return NULL; }
    if (fscanf(f, "%d %d %d", w, h, &maxv) != 3 || *w <= 0 || *h <= 0 || maxv != 255) {
        fclose(f);
        return NULL;
    }
    int c = fgetc(f); /* maxval 后的单个空白 */
    if (c == EOF) { fclose(f); return NULL; }
    size_t n = (size_t)*w * (size_t)*h;
    unsigned char *buf = (unsigned char*)malloc(n);
    if (!buf) { fclose(f); return NULL; }
    if (fread(buf, 1, n, f) != n) { free(buf); fclose(f); return NULL; }
    fclose(f);
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s a.pgm b.pgm\n", argv[0]);
        return 2;
    }
    int wa = 0, ha = 0, wb = 0, hb = 0;
    unsigned char *a = read_pgm(argv[1], &wa, &ha);
    unsigned char *b = read_pgm(argv[2], &wb, &hb);
    if (!a || !b) { fprintf(stderr, "read failed (%s)\n", a ? argv[2] : argv[1]); free(a); free(b); return 2; }
    if (wa != wb || ha != hb) {
        fprintf(stderr, "size mismatch %dx%d vs %dx%d\n", wa, ha, wb, hb);
        free(a); free(b);
        return 1;
    }
    size_t n = (size_t)wa * (size_t)ha, diff = 0, first = 0;
    for (size_t i = 0; i < n; ++i)
        if (a[i] != b[i]) {
            if (diff == 0) first = i;
            ++diff;
        }
    printf("diff %zu/%zu", diff, n);
    if (diff)
        printf(" first(%zu,%zu)=%u,%u", first % (size_t)wa, first / (size_t)wa,
               a[first], b[first]);
    printf("\n");
    free(a); free(b);
    return diff ? 1 : 0;
}
