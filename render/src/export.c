#include "export.h"
#include <stdio.h>
#include <sys/stat.h>

/* 导出文件的像素字节只由 buf 决定；文件权限显式固定为 0644，
 * 不让进程 umask（022/077/…）泄漏到产物上。 */
int export_pgm(const char *path, const unsigned char *buf, int w, int h) {
    if (!path || !buf || w <= 0 || h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    if (fwrite(buf, 1, (size_t)w * (size_t)h, f) != (size_t)w * (size_t)h) {
        fclose(f);
        return -3;
    }
    if (fclose(f) != 0) return -3;
    if (chmod(path, 0644) != 0) return -4;
    return 0;
}

int export_count_on(const unsigned char *buf, int w, int h) {
    if (!buf || w <= 0 || h <= 0) return -1;
    int n = 0;
    for (int i = 0; i < w * h; ++i) if (buf[i]) ++n;
    return n;
}
