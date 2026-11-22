#include "palette.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 手挑调色板：暗部只给了一个 48，暗区 0..48 全部挤向它，导致暗部糊成一块。
 * 这是旧方案，保持不变；新方案由 palette_kmeans 求。顺序即旧逻辑的平局顺序。 */
static const Palette HAND = {
    8,
    {
        {0, 0, 0},
        {48, 48, 48},
        {112, 112, 112},
        {176, 176, 176},
        {240, 240, 240},
        {200, 48, 48},
        {48, 160, 72},
        {56, 96, 200},
    }
};

const Palette *palette_hand(void) { return &HAND; }

static long d2(Rgb a, Rgb b) {
    long dr = (long)a.r - b.r, dg = (long)a.g - b.g, db = (long)a.b - b.b;
    return dr * dr + dg * dg + db * db;
}

static int rgb_less(Rgb a, Rgb b) {
    if (a.r != b.r) return a.r < b.r;
    if (a.g != b.g) return a.g < b.g;
    return a.b < b.b;
}

static int rgb_eq(Rgb a, Rgb b) { return a.r == b.r && a.g == b.g && a.b == b.b; }

/* 最近色下标：距离平局取下标小者（保证确定性）。 */
static int nearest(const Rgb *pal, int n, Rgb p) {
    int best = 0;
    long bd = d2(p, pal[0]);
    for (int i = 1; i < n; ++i) {
        long d = d2(p, pal[i]);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
}

double palette_var_against(const PImage *img, int n, const Rgb *pal) {
    if (!img || !img->pix || img->w <= 0 || img->h <= 0 || n <= 0 || !pal) return -1.0;
    size_t total = (size_t)img->w * (size_t)img->h;
    double sse = 0.0;
    for (size_t i = 0; i < total; ++i) {
        Rgb p = {img->pix[3*i], img->pix[3*i + 1], img->pix[3*i + 2]};
        int j = nearest(pal, n, p);
        sse += (double)d2(p, pal[j]);
    }
    return sse / (double)total;
}

double palette_legacy_var(const PImage *img) {
    return palette_var_against(img, HAND.n, HAND.v);
}

int palette_is_single_color(const PImage *img) {
    if (!img || !img->pix || img->w <= 0 || img->h <= 0) return 0;
    size_t total = (size_t)img->w * (size_t)img->h;
    for (size_t i = 1; i < total; ++i)
        if (img->pix[3*i] != img->pix[0] || img->pix[3*i + 1] != img->pix[1]
            || img->pix[3*i + 2] != img->pix[2]) return 0;
    return 1;
}

/* 组内平方误差：像素到最近代表色的平方距离和，并回填每组统计。
 * assign 可为 NULL（不关心分配时）；counts/gsse 可为 NULL。 */
static double measure(const PImage *img, const Rgb *rep, int k,
                      int *assign, long *counts, double *gsse) {
    size_t total = (size_t)img->w * (size_t)img->h;
    for (int j = 0; j < k; ++j) { if (counts) counts[j] = 0; if (gsse) gsse[j] = 0.0; }
    double sse = 0.0;
    for (size_t i = 0; i < total; ++i) {
        Rgb p = {img->pix[3*i], img->pix[3*i + 1], img->pix[3*i + 2]};
        int j = nearest(rep, k, p);
        long dd = d2(p, rep[j]);
        sse += (double)dd;
        if (assign) assign[i] = j;
        if (counts) counts[j]++;
        if (gsse) gsse[j] += (double)dd;
    }
    return sse;
}

/* 把一个浮点质心四舍五入并夹到 [0,255]；round-half-up，避免 -0.5 方向歧义。 */
static Rgb round_rgb(const double *c) {
    Rgb r;
    unsigned char *dst[3] = {&r.r, &r.g, &r.b};
    for (int i = 0; i < 3; ++i) {
        double v = floor(c[i] + 0.5);
        if (v < 0.0) v = 0.0;
        if (v > 255.0) v = 255.0;
        *dst[i] = (unsigned char)v;
    }
    return r;
}

/* 对现有代表色做整数中心精修：每组取组内均值再四舍五入，直到稳定。
 * 只在整数格点上移动，因此结果确定且不会发散。 */
static void refine_integer(const PImage *img, Rgb *rep, int k, int max_rounds) {
    size_t total = (size_t)img->w * (size_t)img->h;
    int *assign = (int*)malloc(total * sizeof(int));
    if (!assign) return;
    for (int round = 0; round < max_rounds; ++round) {
        measure(img, rep, k, assign, NULL, NULL);
        double sum[PAL_MAX_GROUPS][3];
        long cnt[PAL_MAX_GROUPS];
        memset(sum, 0, sizeof(sum));
        memset(cnt, 0, sizeof(cnt));
        for (size_t i = 0; i < total; ++i) {
            int j = assign[i];
            sum[j][0] += img->pix[3*i];
            sum[j][1] += img->pix[3*i + 1];
            sum[j][2] += img->pix[3*i + 2];
            cnt[j]++;
        }
        Rgb next[PAL_MAX_GROUPS];
        int changed = 0;
        for (int j = 0; j < k; ++j) {
            if (cnt[j] > 0) {
                double c[3] = {sum[j][0] / cnt[j], sum[j][1] / cnt[j], sum[j][2] / cnt[j]};
                next[j] = round_rgb(c);
            } else {
                next[j] = rep[j];
            }
            if (!rgb_eq(next[j], rep[j])) changed = 1;
        }
        /* 精修可能让两个质心取整到同一格：去重并保持 k 不变（空组由上次分配残留）。 */
        for (int j = 0; j < k; ++j) rep[j] = next[j];
        if (!changed) break;
    }
    free(assign);
}

PStatus palette_kmeans(const PImage *img, int k, ClusterResult *out) {
    if (!img || !img->pix || img->w <= 0 || img->h <= 0 || !out) return P_IMG_BAD;
    if (k <= 0) return P_K_INVALID;
    if (k > PAL_MAX_GROUPS) return P_K_TOO_BIG;
    size_t total = (size_t)img->w * (size_t)img->h;

    /* 唯一色统计：单色图单独报出；同时把 k 夹到唯一色数。 */
    int single = palette_is_single_color(img);
    if (single) {
        out->k = 1;
        out->rep[0].r = img->pix[0];
        out->rep[0].g = img->pix[1];
        out->rep[0].b = img->pix[2];
        out->count[0] = (long)total;
        out->gsse[0] = 0.0;
        out->gvar[0] = 0.0;
        out->sse = 0.0;
        out->var = 0.0;
        out->iters = 0;
        return P_SINGLE_COLOR;
    }

    /* maximin 初始化：先取离加权均值最近的像素，再反复取离已选中心最远的像素。
     * 距离平局取字典序小的颜色，保证不同实现得到同一组初值。 */
    double mr = 0.0, mg = 0.0, mb = 0.0;
    Rgb minv = {255, 255, 255}, maxv = {0, 0, 0};
    for (size_t i = 0; i < total; ++i) {
        Rgb p = {img->pix[3*i], img->pix[3*i + 1], img->pix[3*i + 2]};
        mr += p.r; mg += p.g; mb += p.b;
        if (rgb_less(p, minv)) minv = p;
        if (rgb_less(maxv, p)) maxv = p;
    }
    Rgb mean = round_rgb((double[]){mr / (double)total, mg / (double)total, mb / (double)total});
    Rgb centers[PAL_MAX_GROUPS];
    int nc = 0;
    /* 初值 1：离均值最近的实际像素；扫描顺序 + 严格小于 → 平局取先出现者，
     * 由于图本身有序，先出现者不保证字典序，故显式按 (距离, 颜色) 取最小。 */
    {
        long bd = -1;
        Rgb best = {0, 0, 0};
        for (size_t i = 0; i < total; ++i) {
            Rgb p = {img->pix[3*i], img->pix[3*i + 1], img->pix[3*i + 2]};
            long d = d2(p, mean);
            if (bd < 0 || d < bd || (d == bd && rgb_less(p, best))) { bd = d; best = p; }
        }
        centers[nc++] = best;
    }
    while (nc < k) {
        long bd = -1;
        Rgb best = {0, 0, 0};
        for (size_t i = 0; i < total; ++i) {
            Rgb p = {img->pix[3*i], img->pix[3*i + 1], img->pix[3*i + 2]};
            long md = 0;
            for (int j = 0; j < nc; ++j) {
                long d = d2(p, centers[j]);
                if (j == 0 || d < md) md = d;
            }
            if (md == 0) continue;   /* 已是某中心本身 */
            if (bd < 0 || md > bd || (md == bd && rgb_less(p, best))) { bd = md; best = p; }
        }
        if (bd < 0) break;           /* 唯一色不足 k 个：实际组数少于 k */
        centers[nc++] = best;
    }
    k = nc;

    /* Lloyd 迭代：分配到最近中心，再取组内均值。这里用精确（浮点）质心迭代，
     * 只有收敛或触到轮数上限后才取整；否则整数取整会让目标函数来回跳、不保证收敛。
     * 收敛判据为分配不再变化。 */
    double cen[PAL_MAX_GROUPS][3];
    for (int j = 0; j < k; ++j) {
        cen[j][0] = centers[j].r; cen[j][1] = centers[j].g; cen[j][2] = centers[j].b;
    }
    int *assign = (int*)malloc(total * sizeof(int));
    if (!assign) return P_IMG_BAD;
    int *prev = (int*)malloc(total * sizeof(int));
    if (!prev) { free(assign); return P_IMG_BAD; }
    double sum[PAL_MAX_GROUPS][3];
    long cnt[PAL_MAX_GROUPS];
    int iters = 0;
    for (int it = 0; it < 256; ++it) {
        iters = it + 1;
        /* 分配到最近质心；距离用 double 平方欧氏。 */
        for (size_t i = 0; i < total; ++i) {
            double pr = img->pix[3*i], pg = img->pix[3*i + 1], pb = img->pix[3*i + 2];
            int best = 0;
            double bd = (pr - cen[0][0]) * (pr - cen[0][0])
                      + (pg - cen[0][1]) * (pg - cen[0][1])
                      + (pb - cen[0][2]) * (pb - cen[0][2]);
            for (int j = 1; j < k; ++j) {
                double d = (pr - cen[j][0]) * (pr - cen[j][0])
                         + (pg - cen[j][1]) * (pg - cen[j][1])
                         + (pb - cen[j][2]) * (pb - cen[j][2]);
                if (d < bd) { bd = d; best = j; }
            }
            assign[i] = best;
        }
        if (it > 0) {
            int same = 1;
            for (size_t i = 0; i < total; ++i)
                if (assign[i] != prev[i]) { same = 0; break; }
            if (same) break;
        }
        memcpy(prev, assign, total * sizeof(int));
        memset(sum, 0, sizeof(sum));
        memset(cnt, 0, sizeof(cnt));
        for (size_t i = 0; i < total; ++i) {
            int j = assign[i];
            sum[j][0] += img->pix[3*i];
            sum[j][1] += img->pix[3*i + 1];
            sum[j][2] += img->pix[3*i + 2];
            cnt[j]++;
        }
        for (int j = 0; j < k; ++j) {
            if (cnt[j] > 0) {
                cen[j][0] = sum[j][0] / cnt[j];
                cen[j][1] = sum[j][1] / cnt[j];
                cen[j][2] = sum[j][2] / cnt[j];
            }
            /* 空组：保留原地，下一轮由更远的点重新填充。 */
        }
    }
    free(prev);
    /* assign 留给收尾的 measure 复用，稍后统一释放。 */

    /* 浮点质心取整成代表色，再做整数中心精修：代表色必须是整数格点上的
     * 组内均值不动点，这样报出的代表色与报出的方差一致。 */
    for (int j = 0; j < k; ++j) centers[j] = round_rgb(cen[j]);
    refine_integer(img, centers, k, 64);

    /* 按字典序排序，丢弃空组（精修后可能因取整合一而出现）。 */
    for (int i = 1; i < k; ++i) {
        Rgb key = centers[i];
        int j = i - 1;
        while (j >= 0 && rgb_less(key, centers[j])) { centers[j + 1] = centers[j]; --j; }
        centers[j + 1] = key;
    }
    int uniq = 0;
    for (int i = 0; i < k; ++i) {
        if (uniq > 0 && rgb_eq(centers[i], centers[uniq - 1])) continue;
        centers[uniq++] = centers[i];
    }
    k = uniq;

    long *counts = (long*)calloc((size_t)k, sizeof(long));
    double *gsse = (double*)calloc((size_t)k, sizeof(double));
    if (!counts || !gsse) { free(counts); free(gsse); free(assign); return P_IMG_BAD; }
    double sse = measure(img, centers, k, assign, counts, gsse);
    free(assign);

    out->k = k;
    for (int j = 0; j < k; ++j) {
        out->rep[j] = centers[j];
        out->count[j] = counts[j];
        out->gsse[j] = gsse[j];
        out->gvar[j] = counts[j] > 0 ? gsse[j] / (double)counts[j] : 0.0;
    }
    out->sse = sse;
    out->var = sse / (double)total;
    out->iters = iters;
    free(counts);
    free(gsse);
    return P_OK;
}

int palette_apply(const PImage *img, int n, const Rgb *pal, PImage *out) {
    if (!img || !img->pix || img->w <= 0 || img->h <= 0 || n <= 0 || !pal || !out) return -1;
    size_t total = (size_t)img->w * (size_t)img->h;
    unsigned char *pix = (unsigned char*)malloc(total * 3);
    if (!pix) return -2;
    for (size_t i = 0; i < total; ++i) {
        Rgb p = {img->pix[3*i], img->pix[3*i + 1], img->pix[3*i + 2]};
        Rgb c = pal[nearest(pal, n, p)];
        pix[3*i] = c.r; pix[3*i + 1] = c.g; pix[3*i + 2] = c.b;
    }
    out->w = img->w;
    out->h = img->h;
    out->pix = pix;
    return 0;
}

void palette_free(PImage *img) {
    if (!img) return;
    free(img->pix);
    img->pix = NULL;
    img->w = img->h = 0;
}

/* ---------- PGM / PPM ---------- */

/* 跳过空白与 '#' 注释，返回下一个 token 的首个非空白字符。 */
static int next_token(FILE *f, int *c_out) {
    int c;
    for (;;) {
        c = fgetc(f);
        if (c == EOF) return -1;
        if (c == '#') { while (c != EOF && c != '\n') c = fgetc(f); continue; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f') continue;
        break;
    }
    *c_out = c;
    return 0;
}

/* 读一个十进制整数，要求其后紧跟空白（不吞掉它，交给调用方处理）。 */
static int read_uint(FILE *f, int *v_out) {
    int c;
    if (next_token(f, &c) != 0) return -1;
    if (c < '0' || c > '9') return -1;
    int v = 0;
    while (c >= '0' && c <= '9') {
        v = v * 10 + (c - '0');
        if (v > 100000000) return -1;
        c = fgetc(f);
    }
    if (c == EOF) return -1;
    ungetc(c, f);
    *v_out = v;
    return 0;
}

int palette_load(const char *path, PImage *out) {
    if (!path || !out) return -1;
    out->pix = NULL; out->w = out->h = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -2;
    int c1 = fgetc(f), c2 = fgetc(f);
    int rgb = 0;
    if (c1 != 'P' || (c2 != '5' && c2 != '6')) { fclose(f); return -3; }
    rgb = (c2 == '6');
    int w = 0, h = 0, maxval = 0;
    if (read_uint(f, &w) != 0 || read_uint(f, &h) != 0 || read_uint(f, &maxval) != 0) {
        fclose(f); return -3;
    }
    if (w <= 0 || h <= 0 || maxval != 255) { fclose(f); return -3; }
    /* 结尾必须恰好一个空白符，随后是裸数据。 */
    int sep = fgetc(f);
    if (sep == EOF || !(sep == ' ' || sep == '\t' || sep == '\n' || sep == '\r'
                        || sep == '\v' || sep == '\f')) { fclose(f); return -3; }
    size_t total = (size_t)w * (size_t)h;
    unsigned char *pix = (unsigned char*)malloc(total * 3);
    if (!pix) { fclose(f); return -4; }
    if (rgb) {
        if (fread(pix, 1, total * 3, f) != total * 3) { free(pix); fclose(f); return -5; }
    } else {
        unsigned char *g = (unsigned char*)malloc(total);
        if (!g) { free(pix); fclose(f); return -4; }
        if (fread(g, 1, total, f) != total) { free(g); free(pix); fclose(f); return -5; }
        for (size_t i = 0; i < total; ++i) {
            pix[3*i] = pix[3*i + 1] = pix[3*i + 2] = g[i];
        }
        free(g);
    }
    fclose(f);
    out->w = w;
    out->h = h;
    out->pix = pix;
    return 0;
}

int palette_save(const char *path, const PImage *img) {
    if (!path || !img || !img->pix || img->w <= 0 || img->h <= 0) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -2;
    fprintf(f, "P6\n%d %d\n255\n", img->w, img->h);
    size_t total = (size_t)img->w * (size_t)img->h;
    if (fwrite(img->pix, 1, total * 3, f) != total * 3) { fclose(f); return -3; }
    fclose(f);
    return 0;
}
