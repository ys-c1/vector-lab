#include "winnorm.h"
#include "winsplit.h"
#include "winsolve.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "data/wins_four.txt";
    WFileSet raw;
    char err[1024];
    if (wsplit_load(path, &raw, err, sizeof err) != 0) {
        fprintf(stderr, "拒绝：%s\n", err);
        return 2;
    }
    WSet s;
    ws_build(&raw, &s);

    printf("=== 关掉新逻辑：输入顶点原样，只报鞋带面积（自交时符号会骗人）===\n");
    for (int k = 0; k < s.count; ++k) {
        WItem *w = &s.win[k];
        double raw_area = winnorm_signed_area(w->in, w->in_n);
        printf("W%-3d 顶点 %d  输入原样有符号面积 %+12.6f  |面积| %12.6f\n",
               w->id, w->in_n, raw_area, raw_area < 0 ? -raw_area : raw_area);
    }

    printf("\n=== 打开新逻辑：先检测，再按子环面积定向，统一为逆时针 ===\n");
    for (int k = 0; k < s.count; ++k) {
        WItem *w = &s.win[k];
        if (w->rc != WN_OK) {
            printf("W%-3d 拒绝 code=%d %s", w->id, w->rc,
                   winnorm_strerror(w->rc));
            if (w->at >= 0) printf(" 序号 %d", w->at);
            if (w->at2 >= 0) printf("/%d", w->at2);
            printf("\n");
            continue;
        }
        int nf = 0;
        for (int r = 0; r < w->rings; ++r) nf += w->nrm.flipped[r];
        printf("W%-3d 子环 %d  归一化后面积绝对值合计 %.6f  翻转 %d 个:",
               w->id, w->rings, w->total_abs, nf);
        for (int r = 0; r < w->rings; ++r)
            printf(" [子环%d 原符号%+g 归一化后%+g]", r,
                   w->nrm.in_signed_area[r],
                   winnorm_signed_area(w->nrm.ring[r].pts, w->nrm.ring[r].n));
        printf("\n");
    }

    /* 画布：关/开两条路径各出一张图，逐像素比较 */
    WBox b;
    if (ws_bbox(&s, &b) != 0) return 3;
    const int w = 304, h = 48, ss = 4;
    static unsigned char a[304 * 48], n[304 * 48];
    if (ws_render(&s, b, w, h, ss, 0, a) != 0) return 4;
    if (ws_render(&s, b, w, h, ss, 1, n) != 0) return 4;
    long on_a = 0, on_n = 0, d = ws_diff(a, n, w, h);
    for (long i = 0; i < (long)w * h; ++i) {
        if (a[i]) ++on_a;
        if (n[i]) ++on_n;
    }
    printf("\n画布 %dx%d ss=%d：关掉新逻辑 %ld 像素，打开新逻辑 %ld 像素，差异 %ld\n",
           w, h, ss, on_a, on_n, d);
    return 0;
}
