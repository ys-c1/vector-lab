#include "rdq.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HDR_BYTES 18 /* "RQ1" + w4 + h4 + block4 + dmin1 + dmax1 + lossless1 */

int rq_validate(const RQParams *p) {
    if (!p) return -1;
    if (p->w <= 0 || p->h <= 0) return -1;
    if (p->block < 1 || p->block > 64) return -1;
    if (p->dmin < 1 || p->dmax > RQ_DEV_BITS || p->dmin > p->dmax) return -1;
    return 0;
}

static int levels(int d) { return 1 << d; }

/* 4 位设备的 16 级灰阶是 k*17。每块的 2^d 级一律取自这个网格，
 * 因此无论该块选了几位，结果都能在 4 位设备上原样显示。
 * 档位 j 映射到设备级 round(j*15/(2^d-1))，解码端用同一整数公式重建。 */
static int device_level(int q, int d) {
    return (q * (RQ_DEV_LEVELS - 1) + (levels(d) - 1) / 2) / (levels(d) - 1);
}

static unsigned char recon(int q, int d) {
    return (unsigned char)(device_level(q, d) * 255 / (RQ_DEV_LEVELS - 1));
}

/* 每块索引按字节对齐，尺寸可加。 */
static long code_bytes(long px, int d) { return (px * (long)d + 7) / 8; }

/* 块内蛇形 Floyd–Steinberg。位深 d 的调色板是 4 位设备 16 级网格上的
 * 2^d 个等间距点（见 device_level/recon）。
 *
 * 误差扩散只在块内进行，块边界残差丢弃。这样：
 *   - D_i(d) 就是该块最终编码的真实 SSE，不是估计值，分配器用它做
 *     率失真决策时与最终码流完全一致；
 *   - 各块重建互不影响，解码只查本块码字，无跨块状态；
 *   - 阶梯每步严格降低总 SSE，故 PSNR 随预算单调（见下方分配构造）。
 * 代价是块边界不承接邻居误差；实测块缝与块内的平均绝对误差相当
 * （约 0.9–1.03 倍），不会形成可见台阶。 */
static long quant_block(const unsigned char *img, const RQParams *p,
                        int bx, int by, int d,
                        unsigned char *codes, unsigned char *pal) {
    const int bs = p->block;
    const int n = levels(d);
    const int x0 = bx * bs, y0 = by * bs;
    const int ww = p->w - x0 < bs ? p->w - x0 : bs;
    const int hh = p->h - y0 < bs ? p->h - y0 : bs;

    for (int k = 0; k < n; ++k) pal[k] = recon(k, d);

    /* 两行误差缓冲交替；下标 +1 偏置，左右两列各留一格防止越界 */
    double *ra = (double*)calloc((size_t)(bs + 2), sizeof(double));
    double *rb = (double*)calloc((size_t)(bs + 2), sizeof(double));
    if (!ra || !rb) { free(ra); free(rb); return -1; }

    long sse = 0;
    for (int y = 0; y < hh; ++y) {
        const int rev = y & 1;
        for (int xi = 0; xi < ww; ++xi) {
            const int x = rev ? ww - 1 - xi : xi;
            const int orig = img[(size_t)(y0 + y) * p->w + (x0 + x)];
            double v = (double)orig + ra[x + 1];
            if (v < 0.0) v = 0.0;
            if (v > 255.0) v = 255.0;
            /* 在真实设备网格上调色板里取最近级（级数 <=16，直接扫描） */
            int q = 0;
            double bd = fabs(v - (double)pal[0]);
            for (int k = 1; k < n; ++k) {
                const double dd = fabs(v - (double)pal[k]);
                if (dd < bd) { bd = dd; q = k; }
            }
            const double rec = (double)pal[q];
            const double e = v - rec;
            const long dv = (long)orig - (long)rec;
            sse += dv * dv;
            codes[(size_t)y * bs + x] = (unsigned char)q;
            const int dir = rev ? -1 : 1;
            ra[x + 1 + dir] += e * 7.0 / 16.0;
            rb[x + 1 - dir] += e * 3.0 / 16.0;
            rb[x + 1] += e * 5.0 / 16.0;
            rb[x + 1 + dir] += e * 1.0 / 16.0;
        }
        double *t = ra; ra = rb; rb = t;
        memset(rb, 0, sizeof(double) * (size_t)(bs + 2));
    }
    free(ra);
    free(rb);
    return sse;
}

static int image_solid(const unsigned char *img, int w, int h, unsigned char *val) {
    unsigned char v = img[0];
    for (int i = 1; i < w * h; ++i) if (img[i] != v) return 0;
    if (val) *val = v;
    return 1;
}

/* 头 + 位深表 + 每块位打包索引。调色板由位深重建，不占体积。 */
static long plan_bytes(const unsigned char *bits, const long *px, int nblk) {
    long n = HDR_BYTES + nblk;
    for (int i = 0; i < nblk; ++i)
        n += code_bytes(px[i], bits[i]);
    return n;
}

static double psnr_of(long sse, long npix) {
    if (sse <= 0) return INFINITY;
    return 10.0 * log10(255.0 * 255.0 / ((double)sse / (double)npix));
}

int rq_encode(const unsigned char *img, const RQParams *p, long budget,
              RQImage *out, RQStats *st, const char **badparam) {
    if (badparam) *badparam = 0;
    if (!img || !p || !out) { if (badparam) *badparam = "img"; return -1; }
    if (rq_validate(p) != 0) {
        if (badparam) {
            if (!p->w || !p->h) *badparam = "w/h";
            else if (p->block < 1 || p->block > 64) *badparam = "block";
            else *badparam = "dmin/dmax";
        }
        return -1;
    }
    if (budget <= 0) { if (badparam) *badparam = "budget"; return -2; }

    memset(out, 0, sizeof(*out));
    out->w = p->w; out->h = p->h; out->block = p->block;
    out->bw = (p->w + p->block - 1) / p->block;
    out->bh = (p->h + p->block - 1) / p->block;
    out->dmin = p->dmin; out->dmax = p->dmax;
    const int nblk = out->bw * out->bh;
    const int nd = p->dmax - p->dmin + 1;
    const int bs = p->block;
    const long npix = (long)p->w * p->h;

    unsigned char solid = 0;
    if (image_solid(img, p->w, p->h, &solid)) {
        out->lossless = 1;
        out->bits = (unsigned char*)malloc(1);
        out->pal = (unsigned char*)malloc(1);
        if (!out->bits || !out->pal) { rq_free(out); return -1; }
        out->bits[0] = RQ_DEV_BITS;
        out->pal[0] = solid;
        out->nbytes = HDR_BYTES + 1;
        if (out->nbytes > budget) { if (badparam) *badparam = "budget"; rq_free(out); return -2; }
        if (st) { st->psnr = INFINITY; st->sse = 0; st->nbytes = out->nbytes; st->lambda = 0.0; }
        return 0;
    }

    out->bits = (unsigned char*)malloc((size_t)nblk);
    out->codes = (unsigned char*)malloc((size_t)nblk * bs * bs);
    out->pal = (unsigned char*)calloc((size_t)nblk * RQ_DEV_LEVELS, 1);
    long *D = (long*)malloc(sizeof(long) * (size_t)nblk * nd);
    long *px = (long*)malloc(sizeof(long) * nblk);
    int *choice = (int*)malloc(sizeof(int) * nblk);
    unsigned char *tmpc = (unsigned char*)malloc((size_t)bs * bs);
    unsigned char *tmpp = (unsigned char*)malloc(RQ_DEV_LEVELS);
    if (!out->bits || !out->codes || !out->pal || !D || !px || !choice
        || !tmpc || !tmpp) {
        free(D); free(px); free(choice); free(tmpc); free(tmpp);
        rq_free(out);
        return -1;
    }

    /* D[i][k]：块 i 取位深 dmin+k 的真实 SSE */
    for (int by = 0; by < out->bh; ++by)
        for (int bx = 0; bx < out->bw; ++bx) {
            const int i = by * out->bw + bx;
            const int ww = p->w - bx * bs < bs ? p->w - bx * bs : bs;
            const int hh = p->h - by * bs < bs ? p->h - by * bs : bs;
            px[i] = (long)ww * hh;
            for (int k = 0; k < nd; ++k) {
                const long s = quant_block(img, p, bx, by, p->dmin + k, tmpc, tmpp);
                if (s < 0) {
                    free(D); free(px); free(choice); free(tmpc); free(tmpp);
                    rq_free(out);
                    return -1;
                }
                D[i * nd + k] = s;
            }
        }
    free(tmpc); free(tmpp);

    /* 全最低档仍超预算：任务不可行 */
    for (int i = 0; i < nblk; ++i) {
        choice[i] = 0;
        out->bits[i] = (unsigned char)p->dmin;
    }
    long used = plan_bytes(out->bits, px, nblk);
    if (used > budget) {
        free(D); free(px); free(choice);
        rq_free(out);
        if (badparam) *badparam = "budget";
        return -2;
    }

    /* 率失真最优分配。
     *
     * 1) 每块先取 Pareto 前沿：位深 d 只在「没有更低位深同时不差于它」时
     *    保留。误差扩散让 D 未必随 d 单调，前沿保证候选点的 D 严格随
     *    字节数下降。
     * 2) 对前沿取凸包下沿，得到边际斜率严格递减的步序列。
     * 3) 各块的步按斜率做全局归并（每块内部顺序固定），形成与预算无关
     *    的一条阶梯。取阶梯前 m 步即对某个 λ 的拉格朗日最优解。
     *
     * 预算越大 → m 越大 → 累计 SSE 继续下降；每步 ΔSSE>0，故 PSNR 随
     * 预算单调不减，由构造保证。停下的那一步的斜率即该预算的 λ。 */
    double lambda = 0.0;
    {
        typedef struct { double slope; int blk; int to; long dbytes; long dsse; } Step;
        Step *steps = (Step*)malloc(sizeof(Step) * (size_t)nblk * (size_t)nd);
        int *hull = (int*)malloc(sizeof(int) * (size_t)nblk * (size_t)(nd + 1));
        int *hn = (int*)calloc((size_t)nblk, sizeof(int));
        int *cur = (int*)calloc((size_t)nblk, sizeof(int)); /* 每块待走的凸包边 */
        if (!steps || !hull || !hn || !cur) {
            free(steps); free(hull); free(hn); free(cur);
            free(D); free(px); free(choice);
            rq_free(out);
            return -1;
        }
        int nstep = 0;

        for (int i = 0; i < nblk; ++i) {
            /* Pareto 前沿：按位深升序，只留 SSE 严格更低的点 */
            int front[9], fn = 0;
            long best = -1;
            for (int k = 0; k < nd; ++k) {
                const long s = D[i * nd + k];
                if (best < 0 || s < best) { front[fn++] = k; best = s; }
            }
            /* 凸包下沿（可行域是前沿点，斜率严格递减） */
            int *h = hull + (size_t)i * (size_t)(nd + 1);
            h[hn[i]++] = front[0];
            for (int f = 1; f < fn; ++f) {
                const int k = front[f];
                while (hn[i] >= 2) {
                    const int a = h[hn[i] - 2], b = h[hn[i] - 1];
                    const long abx = code_bytes(px[i], p->dmin + b)
                                   - code_bytes(px[i], p->dmin + a);
                    const long aby = D[i * nd + b] - D[i * nd + a];
                    const long bcx = code_bytes(px[i], p->dmin + k)
                                   - code_bytes(px[i], p->dmin + b);
                    const long bcy = D[i * nd + k] - D[i * nd + b];
                    /* 下凸包：slope(a,b) >= slope(b,k) 时 b 不在下沿 */
                    if ((double)aby * (double)bcx >= (double)bcy * (double)abx) --hn[i];
                    else break;
                }
                h[hn[i]++] = k;
            }
        }

        /* 全局归并：反复取各块「下一条凸包边」中斜率最大者。
         * 该顺序只由 D 与块尺寸决定，与预算无关。 */
        for (;;) {
            int bi = -1;
            double bv = -1e300;
            for (int i = 0; i < nblk; ++i) {
                const int c = cur[i];
                if (c + 1 >= hn[i]) continue;
                const int a = hull[(size_t)i * (size_t)(nd + 1) + c];
                const int b = hull[(size_t)i * (size_t)(nd + 1) + c + 1];
                const long db = code_bytes(px[i], p->dmin + b)
                              - code_bytes(px[i], p->dmin + a);
                const long ds = D[i * nd + a] - D[i * nd + b];
                if (ds <= 0) { ++cur[i]; continue; }
                /* 小块的几个档位可能都落在同一字节数上（db==0）：升档免费，
                 * 斜率视为无穷，优先走。 */
                const double sl = db > 0 ? (double)ds / (double)db : 1e300;
                if (sl > bv) { bv = sl; bi = i; }
            }
            if (bi < 0) break;
            const int c = cur[bi];
            const int a = hull[(size_t)bi * (size_t)(nd + 1) + c];
            const int b = hull[(size_t)bi * (size_t)(nd + 1) + c + 1];
            steps[nstep].slope = bv;
            steps[nstep].blk = bi;
            steps[nstep].to = b;
            steps[nstep].dbytes = code_bytes(px[bi], p->dmin + b)
                                - code_bytes(px[bi], p->dmin + a);
            if (steps[nstep].dbytes < 0) steps[nstep].dbytes = 0;
            steps[nstep].dsse = D[bi * nd + a] - D[bi * nd + b];
            ++nstep;
            ++cur[bi];
        }

        /* 吃阶梯的可行前缀 */
        for (int a = 0; a < nstep; ++a) {
            const int i = steps[a].blk;
            if (used + steps[a].dbytes > budget) { lambda = steps[a].slope; break; }
            choice[i] = steps[a].to;
            out->bits[i] = (unsigned char)(p->dmin + choice[i]);
            used += steps[a].dbytes;
        }
        free(steps); free(hull); free(hn); free(cur);
    }

    /* 按选定位深真正编码；SSE 由实际码流重算，stats 即解码图的失真 */
    long sse = 0;
    for (int by = 0; by < out->bh; ++by)
        for (int bx = 0; bx < out->bw; ++bx) {
            const int i = by * out->bw + bx;
            const int d = out->bits[i], n = levels(d);
            unsigned char *pal = out->pal + (size_t)i * RQ_DEV_LEVELS;
            unsigned char *codes = out->codes + (size_t)i * bs * bs;
            for (int k = 0; k < n; ++k) pal[k] = recon(k, d);
            const long s = quant_block(img, p, bx, by, d, codes, pal);
            if (s < 0) {
                free(D); free(px); free(choice);
                rq_free(out);
                return -1;
            }
            sse += s;
        }

    out->nbytes = (int)plan_bytes(out->bits, px, nblk);
    free(D); free(px); free(choice);
    if (out->nbytes > budget) { /* 不变量：绝不超预算 */
        rq_free(out);
        if (badparam) *badparam = "budget";
        return -2;
    }
    if (st) {
        st->sse = sse;
        st->psnr = psnr_of(sse, npix);
        st->nbytes = out->nbytes;
        st->lambda = lambda;
    }
    return 0;
}

int rq_decode(const RQImage *q, unsigned char *out) {
    if (!q || !out) return -1;
    if (q->lossless) {
        memset(out, q->pal[0], (size_t)q->w * q->h);
        return 0;
    }
    if (!q->bits || !q->codes || !q->pal) return -1;
    const int bs = q->block;
    for (int by = 0; by < q->bh; ++by)
        for (int bx = 0; bx < q->bw; ++bx) {
            const int i = by * q->bw + bx;
            const int ww = q->w - bx * bs < bs ? q->w - bx * bs : bs;
            const int hh = q->h - by * bs < bs ? q->h - by * bs : bs;
            const unsigned char *pal = q->pal + (size_t)i * RQ_DEV_LEVELS;
            const unsigned char *codes = q->codes + (size_t)i * bs * bs;
            for (int y = 0; y < hh; ++y)
                for (int x = 0; x < ww; ++x)
                    out[(size_t)(by * bs + y) * q->w + (bx * bs + x)] =
                        pal[codes[(size_t)y * bs + x]];
        }
    return 0;
}

int rq_serialize(const RQImage *q, unsigned char *buf, size_t cap) {
    if (!q || !buf) return -1;
    if (cap < (size_t)q->nbytes) return -2;
    size_t o = 0;
    buf[o++] = 'R'; buf[o++] = 'Q'; buf[o++] = '1';
    unsigned v = (unsigned)q->w; memcpy(buf + o, &v, 4); o += 4;
    v = (unsigned)q->h; memcpy(buf + o, &v, 4); o += 4;
    v = (unsigned)q->block; memcpy(buf + o, &v, 4); o += 4;
    buf[o++] = (unsigned char)q->dmin;
    buf[o++] = (unsigned char)q->dmax;
    buf[o++] = (unsigned char)q->lossless;
    if (q->lossless) { buf[o++] = q->pal[0]; return (int)o; }
    const int nblk = q->bw * q->bh;
    memcpy(buf + o, q->bits, (size_t)nblk); o += (size_t)nblk;
    for (int by = 0; by < q->bh; ++by)
        for (int bx = 0; bx < q->bw; ++bx) {
            const int i = by * q->bw + bx, d = q->bits[i];
            const int ww = q->w - bx * q->block < q->block ? q->w - bx * q->block : q->block;
            const int hh = q->h - by * q->block < q->block ? q->h - by * q->block : q->block;
            const unsigned char *codes = q->codes + (size_t)i * q->block * q->block;
            unsigned acc = 0;
            int nb = 0;
            for (int y = 0; y < hh; ++y)
                for (int x = 0; x < ww; ++x) {
                    acc |= (unsigned)codes[(size_t)y * q->block + x] << nb;
                    nb += d;
                    while (nb >= 8) {
                        buf[o++] = (unsigned char)(acc & 0xFFu);
                        acc >>= 8;
                        nb -= 8;
                    }
                }
            if (nb > 0) buf[o++] = (unsigned char)(acc & 0xFFu);
        }
    return (int)o;
}

int rq_deserialize(const unsigned char *buf, size_t n, RQImage *q) {
    if (!buf || !q || n < HDR_BYTES) return -1;
    if (buf[0] != 'R' || buf[1] != 'Q' || buf[2] != '1') return -1;
    memset(q, 0, sizeof(*q));
    size_t o = 3;
    unsigned v;
    memcpy(&v, buf + o, 4); q->w = (int)v; o += 4;
    memcpy(&v, buf + o, 4); q->h = (int)v; o += 4;
    memcpy(&v, buf + o, 4); q->block = (int)v; o += 4;
    q->dmin = buf[o++]; q->dmax = buf[o++]; q->lossless = buf[o++];
    if (q->w <= 0 || q->h <= 0 || q->block < 1 || q->block > 64) return -1;
    if (q->dmin < 1 || q->dmax > RQ_DEV_BITS || q->dmin > q->dmax) return -1;
    if (q->lossless) {
        if (o >= n) return -1;
        q->pal = (unsigned char*)malloc(1);
        q->bits = (unsigned char*)malloc(1);
        if (!q->pal || !q->bits) { rq_free(q); return -2; }
        q->pal[0] = buf[o++];
        q->bits[0] = RQ_DEV_BITS;
        q->nbytes = (int)o;
        return 0;
    }
    q->bw = (q->w + q->block - 1) / q->block;
    q->bh = (q->h + q->block - 1) / q->block;
    const int nblk = q->bw * q->bh;
    q->bits = (unsigned char*)malloc((size_t)nblk);
    q->pal = (unsigned char*)calloc((size_t)nblk * RQ_DEV_LEVELS, 1);
    q->codes = (unsigned char*)malloc((size_t)nblk * q->block * q->block);
    if (!q->bits || !q->pal || !q->codes) { rq_free(q); return -2; }
    if (o + (size_t)nblk > n) { rq_free(q); return -1; }
    memcpy(q->bits, buf + o, (size_t)nblk); o += (size_t)nblk;
    for (int i = 0; i < nblk; ++i) {
        if (q->bits[i] < q->dmin || q->bits[i] > q->dmax) { rq_free(q); return -1; }
        const int lv = levels(q->bits[i]);
        for (int k = 0; k < lv; ++k)
            q->pal[(size_t)i * RQ_DEV_LEVELS + k] = recon(k, q->bits[i]);
    }
    for (int by = 0; by < q->bh; ++by)
        for (int bx = 0; bx < q->bw; ++bx) {
            const int i = by * q->bw + bx, d = q->bits[i];
            const int ww = q->w - bx * q->block < q->block ? q->w - bx * q->block : q->block;
            const int hh = q->h - by * q->block < q->block ? q->h - by * q->block : q->block;
            unsigned char *codes = q->codes + (size_t)i * q->block * q->block;
            unsigned acc = 0;
            int nb = 0;
            for (int y = 0; y < hh; ++y)
                for (int x = 0; x < ww; ++x) {
                    while (nb < d) {
                        if (o >= n) { rq_free(q); return -1; }
                        acc |= (unsigned)buf[o++] << nb;
                        nb += 8;
                    }
                    codes[(size_t)y * q->block + x] =
                        (unsigned char)(acc & ((1u << d) - 1u));
                    acc >>= d;
                    nb -= d;
                }
        }
    q->nbytes = (int)o;
    return 0;
}

void rq_free(RQImage *q) {
    if (!q) return;
    free(q->bits); free(q->codes); free(q->pal);
    q->bits = q->codes = q->pal = 0;
}
