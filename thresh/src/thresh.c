#include "thresh.h"
#include <stdlib.h>
#include <string.h>

int thresh_load_pgm(const char *path, TImage *out) {
    if (!path || !out) return -1;
    out->w = out->h = 0;
    out->pix = NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return -2;

    /* 词法读取：PGM token 以空白分隔，# 起为注释直到行尾。 */
    int c;
    char magic[3] = {0};
    if (fread(magic, 1, 2, f) != 2 || magic[0] != 'P' || magic[1] != '5') {
        fclose(f);
        return -3;
    }
    int w = 0, h = 0, maxval = 0;
    int *vals[3] = {&w, &h, &maxval};
    for (int k = 0; k < 3; ++k) {
        do {
            do { c = fgetc(f); } while (c != EOF && (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'));
            if (c == '#') { while (c != EOF && c != '\n') c = fgetc(f); }
            else break;
        } while (c != EOF);
        if (c == EOF || c < '0' || c > '9') { fclose(f); return -3; }
        int v = 0;
        while (c >= '0' && c <= '9') {
            v = v * 10 + (c - '0');
            c = fgetc(f);
        }
        ungetc(c, f);
        *vals[k] = v;
    }
    /* maxval 后恰好一个单空白符，随后是裸像素数据。 */
    c = fgetc(f);
    if (w <= 0 || h <= 0 || maxval != 255 || (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != '\v' && c != '\f')) {
        fclose(f);
        return -3;
    }
    size_t n = (size_t)w * (size_t)h;
    unsigned char *pix = (unsigned char*)malloc(n ? n : 1);
    if (!pix) { fclose(f); return -4; }
    if (fread(pix, 1, n, f) != n) {
        free(pix);
        fclose(f);
        return -5;
    }
    fclose(f);
    out->w = w;
    out->h = h;
    out->pix = pix;
    return 0;
}

void thresh_free(TImage *img) {
    if (!img) return;
    free(img->pix);
    img->pix = NULL;
    img->w = img->h = 0;
}

int thresh_save_pgm(const char *path, const TImage *img) {
    if (!path || !img || !img->pix || img->w <= 0 || img->h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P5\n%d %d\n255\n", img->w, img->h);
    size_t n = (size_t)img->w * (size_t)img->h;
    if (fwrite(img->pix, 1, n, f) != n) { fclose(f); return -3; }
    fclose(f);
    return 0;
}

int thresh_hist(const TImage *img, THist *h) {
    if (!img || !h || !img->pix || img->w <= 0 || img->h <= 0) return -1;
    memset(h, 0, sizeof(*h));
    size_t n = (size_t)img->w * (size_t)img->h;
    for (size_t i = 0; i < n; ++i) ++h->hist[img->pix[i]];
    h->total = (long)n;
    return 0;
}

int thresh_is_black(const THist *h) {
    return h && h->total > 0 && h->hist[0] == h->total;
}

int thresh_is_white(const THist *h) {
    return h && h->total > 0 && h->hist[255] == h->total;
}

double thresh_ratio(const THist *h, int t) {
    if (!h || h->total <= 0 || t < 0 || t > 256) return -1.0;
    if (t == 0) return 1.0;
    if (t == 256) return 0.0;
    long on = 0;
    for (int g = t; g <= 255; ++g) on += h->hist[g];
    return (double)on / (double)h->total;
}

TStatus thresh_invert(const THist *h, double lo, double hi, int *out_t, double *out_ratio) {
    if (!h || !out_t || !out_ratio || h->total <= 0) return T_EMPTY;
    if (!(lo >= 0.0 && lo <= 1.0 && hi >= 0.0 && hi <= 1.0 && lo <= hi)) return T_EMPTY;
    if (thresh_is_black(h)) return T_BLACK;
    if (thresh_is_white(h)) return T_WHITE;

    /* 后缀和：ge[t] = 灰度 >= t 的像素数，t 取 [1,255]。 */
    long ge[257];
    ge[256] = 0;
    for (int g = 255; g >= 1; --g) ge[g] = ge[g + 1] + h->hist[g];

    /* F(t) 关于 t 单调不增，从高阈值向低阈值扫描，取首个落入区间的阈值，
     * 即满足条件的最大阈值（二值化最保守的解）。 */
    for (int t = 255; t >= 1; --t) {
        double r = (double)ge[t] / (double)h->total;
        if (r >= lo && r <= hi) {
            *out_t = t;
            *out_ratio = r;
            return T_OK;
        }
    }
    /* t=0 时 F 恒为 1；只有上界允许 1 时才接受（纯黑/纯白已在前面排除）。 */
    if (1.0 >= lo && 1.0 <= hi) {
        *out_t = 0;
        *out_ratio = 1.0;
        return T_OK;
    }
    return T_UNREACH;
}

void thresh_flanking_steps(const THist *h, double lo, double hi,
                           double *r_below, double *r_above) {
    if (r_below) *r_below = -1.0;
    if (r_above) *r_above = -1.0;
    if (!h || h->total <= 0) return;
    long ge[257];
    ge[256] = 0;
    for (int g = 255; g >= 1; --g) ge[g] = ge[g + 1] + h->hist[g];
    /* t 从 1 扫到 255，F 从高到低经过各台阶；t=0 的台阶恒为 1.0。
     * below = 小于 lo 的最大可达台阶（首次下穿即停）；
     * above = 大于 hi 的最小可达台阶（持续覆盖，最后一次最贴近 hi）。 */
    double below = -1.0, above = -1.0;
    if (1.0 > hi) above = 1.0;
    for (int t = 1; t <= 255; ++t) {
        double r = (double)ge[t] / (double)h->total;
        if (r < lo) { below = r; break; }
        if (r > hi) above = r;
    }
    if (r_below) *r_below = below;
    if (r_above) *r_above = above;
}

int thresh_apply_fixed(const TImage *img, int t, TImage *out) {
    if (!img || !out || !img->pix || img->w <= 0 || img->h <= 0 || t < 0 || t > 255) return -1;
    size_t n = (size_t)img->w * (size_t)img->h;
    unsigned char *pix = (unsigned char*)malloc(n ? n : 1);
    if (!pix) return -2;
    for (size_t i = 0; i < n; ++i) pix[i] = img->pix[i] >= (unsigned char)t ? 255 : 0;
    out->w = img->w;
    out->h = img->h;
    out->pix = pix;
    return 0;
}
