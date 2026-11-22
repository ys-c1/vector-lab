/*
 * 两个 PGM 逐字节比对；不一致时打印首个差异偏移与差异字节数。
 * 用法：pgmdiff a.pgm b.pgm
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: %s a.pgm b.pgm\n", argv[0]);
        return 2;
    }
    FILE *fa = fopen(argv[1], "rb");
    FILE *fb = fopen(argv[2], "rb");
    if (!fa || !fb) {
        fprintf(stderr, "MISSING %s %s\n", argv[1], argv[2]);
        return 2;
    }
    unsigned long off = 0, diffs = 0, first = 0;
    int ca, cb;
    for (;;) {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca == EOF && cb == EOF) break;
        ++off;
        if (ca != cb) {
            if (!first) first = off;
            ++diffs;
        }
    }
    fclose(fa);
    fclose(fb);
    if (!diffs) {
        printf("IDENTICAL %s %s (%lu bytes)\n", argv[1], argv[2], off);
        return 0;
    }
    printf("DIFF %s %s: %lu byte(s) differ, first at offset %lu\n",
           argv[1], argv[2], diffs, first);
    return 1;
}
