#include "svgpath.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 用法：demo [路径 [输出.pgm [宽 高]]]
 * 无参数时沿用内置示例，保持原有行为。 */
static int load_path(const char *file, char **out) {
    FILE *f = fopen(file, "r");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -2; }
    long n = ftell(f);
    if (n < 0 || n > (1 << 20)) { fclose(f); return -3; }
    rewind(f);
    char *buf = (char*)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -4; }
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *out = buf;
    return 0;
}

int main(int argc, char **argv) {
    const char *builtin = "M5,5 L45,5 L45,45 L5,45 Z M15,15 L35,15 L35,35 L15,35 Z";
    const char *d = builtin;
    const char *out = "demo.pgm";
    int w = 50, h = 50;
    char *owned = 0;
    if (argc > 1) {
        if (load_path(argv[1], &owned) != 0) { fprintf(stderr, "read path failed\n"); return 2; }
        d = owned;
    }
    if (argc > 2) out = argv[2];
    if (argc > 3) w = atoi(argv[3]);
    if (argc > 4) h = atoi(argv[4]);
    VPath p;
    if (svg_parse(d, &p) != 0) { free(owned); return 1; }
    unsigned char *buf = (unsigned char*)malloc((size_t)w * h);
    if (!buf) { free(owned); return 1; }
    if (svg_fill(&p, w, h, buf) != 0) { free(owned); free(buf); return 1; }
    int on = 0;
    for (int i = 0; i < w * h; ++i) if (buf[i]) ++on;
    if (svg_save_pgm(out, buf, w, h) != 0) { free(owned); free(buf); return 1; }
    printf("segs %d on %d\n", p.count, on);
    free(buf);
    free(owned);
    return 0;
}
