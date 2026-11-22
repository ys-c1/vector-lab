#include "cull.h"
#include <stdlib.h>

double cull_poly_size(const RPoly *p, const RBox *win, int w, int h) {
    if (!p || !win || w <= 0 || h <= 0 || p->n < 1) return 0.0;
    double minx = p->pts[0].x, maxx = p->pts[0].x;
    double miny = p->pts[0].y, maxy = p->pts[0].y;
    for (int i = 1; i < p->n; ++i) {
        if (p->pts[i].x < minx) minx = p->pts[i].x;
        if (p->pts[i].x > maxx) maxx = p->pts[i].x;
        if (p->pts[i].y < miny) miny = p->pts[i].y;
        if (p->pts[i].y > maxy) maxy = p->pts[i].y;
    }
    double lo_x = minx > win->minx ? minx : win->minx;
    double hi_x = maxx < win->maxx ? maxx : win->maxx;
    double lo_y = miny > win->miny ? miny : win->miny;
    double hi_y = maxy < win->maxy ? maxy : win->maxy;
    double cw = hi_x - lo_x, ch = hi_y - lo_y;
    if (!(cw > 0.0) || !(ch > 0.0)) return 0.0;
    double dx = (win->maxx - win->minx) / w;
    double dy = (win->maxy - win->miny) / h;
    if (!(dx > 0.0) || !(dy > 0.0)) return 0.0;
    return (cw / dx) * (ch / dy);
}

typedef struct { double s, c; } SC;

static int sc_cmp(const void *a, const void *b) {
    double d = ((const SC *)a)->s - ((const SC *)b)->s;
    return (d > 0) - (d < 0);
}

int cull_pick_threshold(const double *sizes, const double *covs, int n,
                        double lambda, double *out_t, double *out_cost) {
    if (lambda < 0.0) return -1;
    if (!sizes || !covs || !out_t || !out_cost || n <= 0) return -2;
    SC *a = (SC *)malloc(sizeof(SC) * (size_t)n);
    if (!a) return -3;
    long kept_zero = 0;
    for (int i = 0; i < n; ++i) {
        a[i].s = sizes[i];
        a[i].c = covs[i];
        if (covs[i] == 0.0) ++kept_zero;
    }
    qsort(a, (size_t)n, sizeof(SC), sc_cmp);

    /* 前缀 k：剔除尺寸不超过第 k 个不同尺寸的所有多边形；k=0 为不剔。 */
    double best = lambda * (double)kept_zero;
    int bestk = 0, k = 0, nuniq = 0;
    double culled_cov = 0.0;
    for (int i = 0; i < n;) {
        int j = i;
        while (j < n && a[j].s == a[i].s) ++j;
        for (int t = i; t < j; ++t) {
            culled_cov += a[t].c;
            if (a[t].c == 0.0) --kept_zero;
        }
        i = j;
        ++k;
        double e = culled_cov + lambda * (double)kept_zero;
        if (e <= best) { best = e; bestk = k; }
    }
    nuniq = k;

    double uk = 0.0, unext = 0.0;
    if (bestk > 0) {
        int kk = 0;
        for (int i = 0; i < n;) {
            int j = i;
            while (j < n && a[j].s == a[i].s) ++j;
            ++kk;
            if (kk == bestk) {
                uk = a[i].s;
                unext = (j < n) ? a[j].s : 0.0;
            }
            i = j;
        }
    }
    if (bestk == 0)
        *out_t = a[0].s / 2.0;          /* 不剔：阈值落在最小尺寸之下 */
    else if (bestk == nuniq)
        *out_t = uk + 1.0;              /* 全剔 */
    else
        *out_t = (uk + unext) / 2.0;    /* 落在两组尺寸之间 */
    *out_cost = best;
    free(a);
    return 0;
}
