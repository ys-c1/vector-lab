/* 冻结基线生成器与逐字节比对工具。
 *
 *   palette_tool record <legacy-dir> <out-dir>   用冻结的旧实现生成基线
 *   palette_tool check  <legacy-dir> <base-dir>  用当前旧逻辑重算并逐字节比对
 *
 * 冻结旧实现：手挑 8 色表 + 最近邻，与本模块 palette.c 的旧路径必须完全独立，
 * 这样关闭新逻辑后的逐字节一致才是真验证，而不是自己比自己。
 * 比对对象是 PPM P6 全文件（含头部）。
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 冻结的旧实现：独立手写，不引用 src/palette.c ---- */

static const int HAND[8][3] = {
    {0, 0, 0}, {48, 48, 48}, {112, 112, 112}, {176, 176, 176},
    {240, 240, 240}, {200, 48, 48}, {48, 160, 72}, {56, 96, 200},
};

static int load_ppm(const char *path, int *w, int *h, unsigned char **pix) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char magic[3] = {0};
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0) { fclose(f); return -1; }
    int maxval;
    if (fscanf(f, "%d %d %d", w, h, &maxval) != 3 || maxval != 255) { fclose(f); return -1; }
    int c = fgetc(f);   /* maxval 后的单空白 */
    if (c == EOF) { fclose(f); return -1; }
    size_t n = (size_t)*w * (size_t)*h * 3;
    unsigned char *p = (unsigned char*)malloc(n);
    if (!p) { fclose(f); return -1; }
    if (fread(p, 1, n, f) != n) { free(p); fclose(f); return -1; }
    fclose(f);
    *pix = p;
    return 0;
}

static int save_ppm(const char *path, int w, int h, const unsigned char *pix) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    size_t n = (size_t)w * (size_t)h * 3;
    if (fwrite(pix, 1, n, f) != n) { fclose(f); return -1; }
    fclose(f);
    return 0;
}

static int frozen_legacy(const char *in, const char *out) {
    int w, h;
    unsigned char *pix;
    if (load_ppm(in, &w, &h, &pix) != 0) return -1;
    unsigned char *q = (unsigned char*)malloc((size_t)w * h * 3);
    if (!q) { free(pix); return -1; }
    for (int i = 0; i < w * h; ++i) {
        int best = 0, bd = 1 << 30;
        for (int j = 0; j < 8; ++j) {
            int dr = pix[3*i] - HAND[j][0], dg = pix[3*i + 1] - HAND[j][1],
                db = pix[3*i + 2] - HAND[j][2];
            int d = dr * dr + dg * dg + db * db;
            if (d < bd) { bd = d; best = j; }   /* 距离平局取下标小者 */
        }
        q[3*i] = (unsigned char)HAND[best][0];
        q[3*i + 1] = (unsigned char)HAND[best][1];
        q[3*i + 2] = (unsigned char)HAND[best][2];
    }
    free(pix);
    int rc = save_ppm(out, w, h, q);
    free(q);
    return rc;
}

/* ---- 命令 ---- */

static int cmd_record(const char *src, const char *dst) {
    const char *names[] = {"dark", "gradient", "flat", "mixed", "solid"};
    for (int i = 0; i < 5; ++i) {
        char a[512], b[512];
        snprintf(a, sizeof(a), "%s/%s.ppm", src, names[i]);
        snprintf(b, sizeof(b), "%s/%s.ppm", dst, names[i]);
        if (frozen_legacy(a, b) != 0) {
            fprintf(stderr, "record failed: %s\n", a);
            return 2;
        }
        printf("record %s\n", b);
    }
    return 0;
}

static int cmd_check(const char *cur, const char *base) {
    const char *names[] = {"dark", "gradient", "flat", "mixed", "solid"};
    int bad = 0;
    for (int i = 0; i < 5; ++i) {
        char a[512], b[512];
        snprintf(a, sizeof(a), "%s/%s.ppm", cur, names[i]);
        snprintf(b, sizeof(b), "%s/%s.ppm", base, names[i]);
        FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
        if (!fa || !fb) {
            printf("MISSING %s %s\n", a, b);
            if (fa) fclose(fa);
            if (fb) fclose(fb);
            bad = 1;
            continue;
        }
        int c1, c2;
        size_t off = 0;
        int diff = 0;
        for (;;) {
            c1 = fgetc(fa); c2 = fgetc(fb);
            if (c1 != c2) { diff = 1; break; }
            if (c1 == EOF) break;
            ++off;
        }
        fclose(fa); fclose(fb);
        if (diff) { printf("DIFF %s byte=%zu\n", names[i], off); bad = 1; }
        else printf("IDENTICAL %s %s\n", names[i], a);
    }
    return bad;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage:\n"
                        "  palette_tool record <legacy-dir> <out-dir>\n"
                        "  palette_tool check  <legacy-dir> <base-dir>\n");
        return 1;
    }
    if (!strcmp(argv[1], "record")) return cmd_record(argv[2], argv[3]);
    if (!strcmp(argv[1], "check")) return cmd_check(argv[2], argv[3]);
    return 1;
}
