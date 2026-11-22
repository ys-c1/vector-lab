#include "src/thresh.h"
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static TImage make_img(int w, int h, const unsigned char *vals) {
    TImage im;
    im.w = w; im.h = h;
    size_t n = (size_t)w * h;
    im.pix = (unsigned char*)malloc(n);
    for (size_t i = 0; i < n; ++i) im.pix[i] = vals[i];
    return im;
}

int main(void) {
    /* 10 个像素，灰度 0..9，每级一个：F(t)= (10-t)/10，t in [1,10) 语义对齐到 256 级。 */
    unsigned char v10[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
    TImage im = make_img(5, 2, v10);
    THist h;
    CK(thresh_hist(&im, &h) == 0);
    CK(h.total == 10);
    CK(thresh_ratio(&h, 0) == 1.0);
    CK(thresh_ratio(&h, 256) == 0.0);
    CK(thresh_ratio(&h, 6) == 0.4);   /* 灰度 >=6：6,7,8,9 共 4 个 */
    CK(thresh_ratio(&h, 1) == 0.9);

    /* 单调性：阈值升高，占比不增。 */
    double prev = 1.0;
    int mono = 1;
    for (int t = 0; t <= 256; ++t) {
        double r = thresh_ratio(&h, t);
        if (r > prev + 1e-12) mono = 0;
        prev = r;
    }
    CK(mono);

    /* 反解：目标 0.4 -> 阈值 6。 */
    int t; double r;
    CK(thresh_invert(&h, 0.39, 0.41, &t, &r) == T_OK);
    CK(t == 6);
    CK(r >= 0.39 && r <= 0.41);

    /* 不可达：0.45 落在 0.4 与 0.5 的台阶之间。 */
    CK(thresh_invert(&h, 0.44, 0.46, &t, &r) == T_UNREACH);
    {
        double below, above;
        thresh_flanking_steps(&h, 0.44, 0.46, &below, &above);
        CK(below == 0.4 && above == 0.5);
        /* 目标高于任何 t>=1 的可达占比：低侧台阶 0.9，高侧只有 t=0 的 1.0。 */
        thresh_flanking_steps(&h, 0.95, 0.96, &below, &above);
        CK(below == 0.9 && above == 1.0);
        /* 目标低于任何正占比：低侧落到 0.0（需要 t>9，已超出图中最大灰度），高侧 0.1。 */
        thresh_flanking_steps(&h, 0.01, 0.02, &below, &above);
        CK(below == 0.0 && above == 0.1);
    }

    /* 非法区间。 */
    CK(thresh_invert(&h, 0.8, 0.7, &t, &r) == T_EMPTY);
    CK(thresh_invert(&h, -0.1, 0.5, &t, &r) == T_EMPTY);
    CK(thresh_invert(&h, 0.5, 1.2, &t, &r) == T_EMPTY);

    /* 区间含端点且覆盖某个台阶即可达。 */
    CK(thresh_invert(&h, 0.5, 0.5, &t, &r) == T_OK && t == 5 && r == 0.5);

    /* 纯黑 / 纯白单独识别。 */
    unsigned char blacks[4] = {0, 0, 0, 0};
    unsigned char whites[4] = {255, 255, 255, 255};
    TImage bimg = make_img(2, 2, blacks), wimg = make_img(2, 2, whites);
    THist bh, wh;
    thresh_hist(&bimg, &bh);
    thresh_hist(&wimg, &wh);
    CK(thresh_is_black(&bh) && !thresh_is_white(&bh));
    CK(thresh_is_white(&wh) && !thresh_is_black(&wh));
    CK(thresh_invert(&bh, 0.0, 0.0, &t, &r) == T_BLACK);
    CK(thresh_invert(&wh, 1.0, 1.0, &t, &r) == T_WHITE);
    CK(thresh_ratio(&bh, 128) == 0.0);
    CK(thresh_ratio(&wh, 255) == 1.0);

    /* 二值化：gray >= t -> 255。 */
    TImage bin;
    CK(thresh_apply_fixed(&im, 6, &bin) == 0);
    long on = 0;
    for (int i = 0; i < 10; ++i) if (bin.pix[i] == 255) ++on;
    CK(on == 4);
    CK(thresh_apply_fixed(&im, -1, &bin) == -1);
    CK(thresh_apply_fixed(&im, 256, &bin) == -1);
    free(bin.pix);

    /* 固定阈值越界拒绝。 */
    CK(thresh_apply_fixed(&im, 0, &bin) == 0);
    CK(bin.pix[0] == 255);   /* t=0：全部亮 */
    free(bin.pix);

    /* PGM 往返：写后读回逐字节一致。 */
    CK(thresh_save_pgm("build/roundtrip.pgm", &im) == 0);
    TImage back;
    CK(thresh_load_pgm("build/roundtrip.pgm", &back) == 0);
    CK(back.w == 5 && back.h == 2);
    int same = 1;
    for (int i = 0; i < 10; ++i) if (back.pix[i] != im.pix[i]) same = 0;
    CK(same);
    thresh_free(&back);

    /* 坏 magic / 坏 maxval 拒绝。 */
    FILE *bad = fopen("build/bad.pgm", "wb");
    fputs("P2\n2 2\n255\n", bad);
    fputc(0, bad); fputc(1, bad); fputc(2, bad); fputc(3, bad);
    fclose(bad);
    TImage junk;
    CK(thresh_load_pgm("build/bad.pgm", &junk) != 0);

    free(im.pix); free(bimg.pix); free(wimg.pix);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
