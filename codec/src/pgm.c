#include "pgm.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

static int read_token(FILE *f, char *tok, size_t n) {
    int c;
    do {
        do { c = fgetc(f); } while (c != EOF && isspace(c));
        if (c == '#') { while ((c = fgetc(f)) != EOF && c != '\n') {} }
    } while (c == '#');
    if (c == EOF) return -1;
    size_t i = 0;
    while (c != EOF && !isspace(c) && i + 1 < n) {
        tok[i++] = (char)c;
        c = fgetc(f);
    }
    if (c != EOF) ungetc(c, f); /* 退回终止分隔符，P5 时它正是数据前的空白 */
    tok[i] = '\0';
    return 0;
}

int pgm_read(const char *path, uint8_t **px, int *w, int *h) {
    if (!path || !px || !w || !h) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) return -2;
    char magic[8], tok[16];
    if (read_token(f, magic, sizeof(magic)) != 0) { fclose(f); return -3; }
    int p5 = (magic[0] == 'P' && magic[1] == '5');
    int p2 = (magic[0] == 'P' && magic[1] == '2');
    if (!p5 && !p2) { fclose(f); return -4; }
    if (read_token(f, tok, sizeof(tok)) != 0) { fclose(f); return -5; }
    int W = atoi(tok);
    if (read_token(f, tok, sizeof(tok)) != 0) { fclose(f); return -5; }
    int H = atoi(tok);
    if (read_token(f, tok, sizeof(tok)) != 0) { fclose(f); return -5; }
    int maxv = atoi(tok);
    if (W <= 0 || H <= 0 || maxv <= 0 || maxv > 65535) { fclose(f); return -6; }
    size_t n = (size_t)W * H;
    uint8_t *buf = (uint8_t*)malloc(n ? n : 1);
    if (!buf) { fclose(f); return -7; }
    if (p5) {
        int c = fgetc(f); /* 单个空白分隔符 */
        (void)c;
        if (maxv < 256) {
            if (fread(buf, 1, n, f) != n) { free(buf); fclose(f); return -8; }
        } else {
            for (size_t i = 0; i < n; ++i) {
                int hi = fgetc(f), lo = fgetc(f);
                if (hi == EOF || lo == EOF) { free(buf); fclose(f); return -8; }
                buf[i] = (uint8_t)(((hi << 8 | lo) * 255) / maxv);
            }
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (read_token(f, tok, sizeof(tok)) != 0) { free(buf); fclose(f); return -8; }
            long v = atol(tok);
            buf[i] = (uint8_t)(maxv == 255 ? v : v * 255 / maxv);
        }
    }
    fclose(f);
    *px = buf; *w = W; *h = H;
    return 0;
}

int pgm_write_p5(const char *path, const uint8_t *px, int w, int h) {
    if (!path || !px || w <= 0 || h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    if (fwrite(px, 1, (size_t)w * (size_t)h, f) != (size_t)w * (size_t)h) {
        fclose(f); return -3;
    }
    fclose(f);
    return 0;
}
