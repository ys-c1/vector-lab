#include "src/palette.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static PImage make(const unsigned char *data, int w, int h) {
    PImage im;
    im.w = w; im.h = h;
    im.pix = (unsigned char*)malloc((size_t)w * h * 3);
    memcpy(im.pix, data, (size_t)w * h * 3);
    return im;
}

static PImage solid_rgb(int w, int h, int r, int g, int b) {
    PImage im;
    im.w = w; im.h = h;
    im.pix = (unsigned char*)malloc((size_t)w * h * 3);
    for (int i = 0; i < w * h; ++i) {
        im.pix[3*i] = (unsigned char)r;
        im.pix[3*i + 1] = (unsigned char)g;
        im.pix[3*i + 2] = (unsigned char)b;
    }
    return im;
}

int main(void) {
    /* ---- 手挑调色板本身 ---- */
    const Palette *hp = palette_hand();
    CK(hp->n == 8);
    CK(hp->v[1].r == 48);

    /* 最近邻方差：单色 = 到最近手的色距离平方。 */
    PImage black = solid_rgb(4, 4, 0, 0, 0);
    CK(palette_legacy_var(&black) == 0.0);
    PImage grey = solid_rgb(4, 4, 20, 20, 20);
    /* 最近手色是 0（3*20^2=1200），比 48（3*28^2=2352）近。 */
    CK(fabs(palette_legacy_var(&grey) - 1200.0) < 1e-9);
    palette_free(&black);
    palette_free(&grey);

    /* ---- 单色图单独报告 ---- */
    PImage one = solid_rgb(5, 3, 72, 72, 72);
    CK(palette_is_single_color(&one) == 1);
    ClusterResult r;
    CK(palette_kmeans(&one, 8, &r) == P_SINGLE_COLOR);
    CK(r.k == 1 && r.rep[0].r == 72 && r.rep[0].g == 72 && r.rep[0].b == 72);
    CK(r.count[0] == 15 && r.var == 0.0 && r.iters == 0);
    palette_free(&one);

    /* ---- 组数为零 / 超过上限 拒绝并说明 ---- */
    unsigned char two[2 * 3] = {0, 0, 0, 255, 255, 255};
    PImage pair = make(two, 2, 1);
    CK(palette_kmeans(&pair, 0, &r) == P_K_INVALID);
    CK(palette_kmeans(&pair, -3, &r) == P_K_INVALID);
    CK(palette_kmeans(&pair, PAL_MAX_GROUPS + 1, &r) == P_K_TOO_BIG);
    CK(palette_kmeans(&pair, PAL_MAX_GROUPS, &r) == P_OK);
    /* 两个唯一色，k=32 也只能得到 2 个非空组。 */
    CK(r.k == 2 && r.count[0] == 1 && r.count[1] == 1);
    PImage empty;
    empty.w = empty.h = 0;
    empty.pix = NULL;
    CK(palette_kmeans(&empty, 8, &r) == P_IMG_BAD);

    /* ---- 两簇：代表色应落在每簇的中点，组内方差只剩噪声 ---- */
    enum { N = 200 };
    unsigned char two_cluster[N * 3];
    for (int i = 0; i < N; ++i) {
        int v = (i & 1) ? 200 + (i % 5) : 20 + (i % 5);
        two_cluster[3*i] = two_cluster[3*i + 1] = two_cluster[3*i + 2] = (unsigned char)v;
    }
    PImage tc = make(two_cluster, N / 2, 2);
    CK(palette_kmeans(&tc, 2, &r) == P_OK);
    CK(r.k == 2);
    CK(r.count[0] + r.count[1] == N);
    /* 两簇入口都超过 8 个唯一色，方差应远小于到最近代表色的粗分。 */
    double v2 = r.var;
    CK(palette_kmeans(&tc, 8, &r) == P_OK);
    CK(r.k == 8);
    CK(r.var < v2);
    /* 组方差之和 * 组数 = 总平方误差。 */
    double acc = 0.0;
    long cnt = 0;
    for (int j = 0; j < r.k; ++j) { acc += r.gsse[j]; cnt += r.count[j]; }
    CK(cnt == N);
    CK(fabs(acc - r.sse) < 1e-6);
    CK(fabs(r.var - r.sse / N) < 1e-9);
    for (int j = 0; j < r.k; ++j)
        CK(fabs(r.gvar[j] - r.gsse[j] / r.count[j]) < 1e-9);

    /* 代表色按字典序且互不相同。 */
    int sorted = 1, uniq = 1;
    for (int j = 1; j < r.k; ++j) {
        Rgb a = r.rep[j - 1], b = r.rep[j];
        if (a.r > b.r || (a.r == b.r && (a.g > b.g || (a.g == b.g && a.b >= b.b)))) sorted = 0;
        if (a.r == b.r && a.g == b.g && a.b == b.b) uniq = 0;
    }
    CK(sorted && uniq);

    /* 确定性：同样输入重复调用结果完全相同。 */
    ClusterResult r2;
    CK(palette_kmeans(&tc, 8, &r2) == P_OK);
    int same = r2.k == r.k && r2.iters == r.iters && fabs(r2.sse - r.sse) < 1e-12;
    for (int j = 0; j < r.k; ++j)
        if (r2.rep[j].r != r.rep[j].r || r2.rep[j].g != r.rep[j].g || r2.rep[j].b != r.rep[j].b) same = 0;
    CK(same);
    palette_free(&tc);
    palette_free(&pair);

    /* ---- 代表色是整数格点不动点：把每个像素分配到最近代表色后，
     *      组内均值四舍五入应仍等于代表色（或收敛在同样分组上） ---- */
    unsigned char grad[16 * 3];
    for (int i = 0; i < 16; ++i) {
        grad[3*i] = (unsigned char)(i * 16);
        grad[3*i + 1] = (unsigned char)(i * 8);
        grad[3*i + 2] = (unsigned char)(i * 4);
    }
    PImage g16 = make(grad, 8, 2);
    CK(palette_kmeans(&g16, 2, &r) == P_OK);
    CK(r.k == 2);
    double handv = palette_legacy_var(&g16);
    double kmv = r.var;
    printf("grad16 hand_var=%.3f cluster_var=%.3f reduce=%.2f%%\n",
           handv, kmv, handv > 0 ? (handv - kmv) / handv * 100.0 : 0.0);
    CK(kmv <= handv);
    palette_free(&g16);

    /* ---- 量化输出只含调色板颜色，且每像素为最近色 ---- */
    const Palette *hp2 = palette_hand();
    unsigned char raw[4 * 3] = {10, 10, 10, 130, 130, 130, 250, 40, 40, 40, 200, 40};
    PImage rin = make(raw, 2, 2);
    PImage qout;
    CK(palette_apply(&rin, hp2->n, hp2->v, &qout) == 0);
    int inpal = 1;
    for (int i = 0; i < 4; ++i) {
        int found = 0;
        for (int j = 0; j < hp2->n; ++j)
            if (qout.pix[3*i] == hp2->v[j].r && qout.pix[3*i + 1] == hp2->v[j].g
                && qout.pix[3*i + 2] == hp2->v[j].b) found = 1;
        if (!found) inpal = 0;
    }
    CK(inpal);
    CK(palette_apply(&rin, 0, hp2->v, &qout) == -1);
    free(qout.pix);
    palette_free(&rin);

    /* ---- PPM 往返 ---- */
    PImage wr = solid_rgb(3, 2, 5, 6, 7);
    CK(palette_save("build/roundtrip.ppm", &wr) == 0);
    PImage back;
    CK(palette_load("build/roundtrip.ppm", &back) == 0);
    CK(back.w == 3 && back.h == 2 && back.pix[0] == 5 && back.pix[5] == 7);
    palette_free(&back);
    palette_free(&wr);

    /* ---- PGM 灰度读入扩成 RGB ---- */
    FILE *pg = fopen("build/grey.pgm", "wb");
    fputs("P5\n2 1\n255\n", pg);
    fputc(0, pg); fputc(255, pg);
    fclose(pg);
    PImage gi;
    CK(palette_load("build/grey.pgm", &gi) == 0);
    CK(gi.pix[0] == 0 && gi.pix[2] == 0 && gi.pix[3] == 255 && gi.pix[5] == 255);
    palette_free(&gi);

    /* ---- 坏文件拒绝 ---- */
    FILE *bad = fopen("build/bad.ppm", "wb");
    fputs("P3\n2 1\n255\n", bad);
    fputc(0, bad); fputc(0, bad); fputc(0, bad);
    fclose(bad);
    PImage junk;
    CK(palette_load("build/bad.ppm", &junk) != 0);
    CK(palette_load("build/nope.ppm", &junk) != 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
