#include "export.h"
#include <stdio.h>

int export_pgm(const char *path, const unsigned char *buf, int w, int h) {
    if (!path || !buf || w <= 0 || h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    return 0;
}

int export_count_on(const unsigned char *buf, int w, int h) {
    if (!buf || w <= 0 || h <= 0) return -1;
    int n = 0;
    for (int i = 0; i < w * h; ++i) if (buf[i]) ++n;
    return n;
}
