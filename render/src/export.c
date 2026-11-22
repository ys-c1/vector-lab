#include "export.h"
#include <stdio.h>

int export_pgm_header_len(int w, int h) {
    if (w <= 0 || h <= 0) return -1;
    int n = snprintf(NULL, 0, "P5\n%d %d\n255\n", w, h);
    return n < 0 ? -1 : n;
}

long long export_pgm_bytes(int w, int h) {
    int hdr = export_pgm_header_len(w, h);
    if (hdr < 0) return -1;
    long long px = (long long)w * (long long)h;
    /* 防御性溢出检查。 */
    if (px > 900000000000000000LL) return -1;
    return (long long)hdr + px;
}

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
