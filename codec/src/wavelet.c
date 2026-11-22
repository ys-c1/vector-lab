#include "wavelet.h"
#include <stdlib.h>

/* 数学定义的 floor 除（向负无穷取整），保证整数提升可逆。 */
static int floor_div2(int x) { return (x >= 0) ? (x >> 1) : -((-x + 1) >> 1); }

int wavelet_levels(int w, int h) {
    int n = (w < h ? w : h);
    int L = 0;
    while (n >= 2 && L < 6) { n >>= 1; ++L; }
    return L < 1 ? 1 : L;
}

/* 一维整数 Haar 正变换：in[0..n) -> lo[0..ceil(n/2))、hi[0..floor(n/2))，
 * lo、hi 为两块独立连续缓冲。 */
static void haar_fwd_1d(const int32_t *in, int n, int32_t *lo, int32_t *hi) {
    int nh = n >> 1;
    for (int i = 0; i < nh; ++i) {
        int32_t l = in[2 * i], r = in[2 * i + 1];
        int32_t d = r - l;
        hi[i] = d;
        lo[i] = l + floor_div2((int)d); /* floor((l+r)/2) */
    }
    if (n & 1) lo[nh] = in[n - 1];
}

static void haar_inv_1d(const int32_t *lo, const int32_t *hi, int n, int32_t *out) {
    int nh = n >> 1;
    for (int i = 0; i < nh; ++i) {
        int32_t d = hi[i];
        int32_t l = lo[i] - floor_div2((int)d);
        out[2 * i] = l;
        out[2 * i + 1] = l + d;
    }
    if (n & 1) out[n - 1] = lo[nh];
}

int wavelet_forward(int32_t *a, int w, int h, int levels, WBand *bands) {
    if (!a || w <= 0 || h <= 0 || levels < 1 || !bands) return -1;
    int m = w > h ? w : h;
    int32_t *tmp = (int32_t*)malloc((size_t)m * sizeof(int32_t));
    int32_t *lo  = (int32_t*)malloc((size_t)m * sizeof(int32_t));
    int32_t *hi  = (int32_t*)malloc((size_t)m * sizeof(int32_t));
    if (!tmp || !lo || !hi) { free(tmp); free(lo); free(hi); return -2; }

    int pw = w, ph = h, nb = 0;
    for (int lev = 0; lev < levels; ++lev) {
        int cw = (pw + 1) >> 1, cq = pw >> 1;   /* 低/高段宽度 */
        int ch = (ph + 1) >> 1, rq = ph >> 1;   /* 低/高段高度 */
        /* 行方向：低段留左、高段放右（始终作用在左上 pw×ph 区域） */
        for (int y = 0; y < ph; ++y) {
            int32_t *row = a + y * w;
            for (int x = 0; x < pw; ++x) tmp[x] = row[x];
            haar_fwd_1d(tmp, pw, lo, hi);
            for (int x = 0; x < cw; ++x) row[x] = lo[x];
            for (int x = 0; x < cq; ++x) row[cw + x] = hi[x];
        }
        /* 列方向：低段留上、高段放下 */
        for (int x = 0; x < pw; ++x) {
            int32_t *col = a + x;
            for (int y = 0; y < ph; ++y) tmp[y] = col[y * w];
            haar_fwd_1d(tmp, ph, lo, hi);
            for (int y = 0; y < ch; ++y) col[y * w] = lo[y];
            for (int y = 0; y < rq; ++y) col[(ch + y) * w] = hi[y];
        }
        /* 该级三块高频：HL(上右) LH(下左) HH(下右)，坐标均为全图坐标 */
        bands[1 + nb++] = (WBand){cw, 0,  cq, ch};
        bands[1 + nb++] = (WBand){0,  ch, cw, rq};
        bands[1 + nb++] = (WBand){cw, ch, cq, rq};
        pw = cw; ph = ch; /* 下一级只在左上 LL 上继续 */
    }
    bands[0] = (WBand){0, 0, pw, ph}; /* 最粗 LL */
    free(tmp); free(lo); free(hi);
    return 1 + nb;
}

int wavelet_layout(int w, int h, int levels, WBand *bands) {
    if (w <= 0 || h <= 0 || levels < 1 || !bands) return -1;
    int pw = w, ph = h, nb = 0;
    for (int lev = 0; lev < levels; ++lev) {
        int cw = (pw + 1) >> 1, cq = pw >> 1;
        int ch = (ph + 1) >> 1, rq = ph >> 1;
        bands[1 + nb++] = (WBand){cw, 0,  cq, ch};
        bands[1 + nb++] = (WBand){0,  ch, cw, rq};
        bands[1 + nb++] = (WBand){cw, ch, cq, rq};
        pw = cw; ph = ch;
    }
    bands[0] = (WBand){0, 0, pw, ph};
    return 1 + nb;
}

int wavelet_inverse(int32_t *a, int w, int h, int levels) {
    if (!a || w <= 0 || h <= 0 || levels < 1) return -1;
    int m = w > h ? w : h;
    int32_t *tmp = (int32_t*)malloc((size_t)m * sizeof(int32_t));
    int32_t *lo  = (int32_t*)malloc((size_t)m * sizeof(int32_t));
    int32_t *hi  = (int32_t*)malloc((size_t)m * sizeof(int32_t));
    if (!tmp || !lo || !hi) { free(tmp); free(lo); free(hi); return -2; }

    /* 每一级作用区域的边长，自最粗级向细级回溯 */
    int gw[8], gh[8];
    int pw = w, ph = h;
    for (int lev = 0; lev < levels; ++lev) {
        gw[lev] = pw; gh[lev] = ph;
        pw = (pw + 1) >> 1; ph = (ph + 1) >> 1;
    }
    for (int k = levels - 1; k >= 0; --k) {
        int W = gw[k], H = gh[k];
        int cw = (W + 1) >> 1, ch = (H + 1) >> 1;
        /* 列方向逆变换：低段 col[0..ch)、高段 col[ch..H) */
        for (int x = 0; x < W; ++x) {
            int32_t *col = a + x;
            for (int y = 0; y < ch; ++y) lo[y] = col[y * w];
            for (int y = 0; y < (H >> 1); ++y) hi[y] = col[(ch + y) * w];
            haar_inv_1d(lo, hi, H, tmp);
            for (int y = 0; y < H; ++y) col[y * w] = tmp[y];
        }
        /* 行方向逆变换：低段 row[0..cw)、高段 row[cw..W) */
        for (int y = 0; y < H; ++y) {
            int32_t *row = a + y * w;
            for (int x = 0; x < cw; ++x) lo[x] = row[x];
            for (int x = 0; x < (W >> 1); ++x) hi[x] = row[cw + x];
            haar_inv_1d(lo, hi, W, tmp);
            for (int x = 0; x < W; ++x) row[x] = tmp[x];
        }
    }
    free(tmp); free(lo); free(hi);
    return 0;
}
