#include "sbc.h"
#include "wavelet.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

/* ---------------- 变长整数：zigzag + LEB128 ---------------- */
static void put_uvar(uint8_t **p, const uint8_t *end, uint64_t v) {
    while (v >= 0x80) {
        if (*p < end) *(*p)++ = (uint8_t)(v | 0x80u);
        v >>= 7;
    }
    if (*p < end) *(*p)++ = (uint8_t)v;
}

static int get_uvar(const uint8_t **p, const uint8_t *end, uint64_t *out) {
    uint64_t v = 0;
    int shift = 0;
    for (;;) {
        if (*p >= end || shift > 63) return -1;
        uint8_t c = *(*p)++;
        v |= (uint64_t)(c & 0x7fu) << shift;
        if (!(c & 0x80u)) break;
        shift += 7;
    }
    *out = v;
    return 0;
}

static uint64_t zz_enc(int64_t x) {
    return (uint64_t)((x << 1) ^ (x >> 63));
}
static int64_t zz_dec(uint64_t u) {
    return (int64_t)(u >> 1) ^ -(int64_t)(u & 1u);
}

/* ---------------- 容量与工具 ---------------- */
size_t sbc_max_size(int w, int h) {
    /* 头 + 每系数最坏 10 字节 varint + deflate 冗余 */
    return SBC_HEADER_SIZE + (size_t)w * h * 10u + 256u;
}

double sbc_psnr(const uint8_t *a, const uint8_t *b, size_t n) {
    if (!a || !b || n == 0) return -1.0;
    double mse = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double d = (double)a[i] - (double)b[i];
        mse += d * d;
    }
    mse /= (double)n;
    if (mse == 0.0) return 99.0;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

/* ---------------- 编码 ---------------- */
int sbc_encode(const uint8_t *px, int w, int h, QPreset q, uint8_t *out, size_t cap) {
    if (!px || !out || w <= 0 || h <= 0 || w > SBC_MAX_DIM || h > SBC_MAX_DIM)
        return -1;
    int base_q16 = quant_preset_base(q);
    if (base_q16 <= 0) return -2;

    size_t npix = (size_t)w * h;
    int32_t *coef = (int32_t*)malloc(npix * sizeof(int32_t));
    WBand *bands = (WBand*)malloc(25 * sizeof(WBand));
    QBandStat *st = NULL;
    uint8_t *raw = NULL;
    int rc = -3;
    if (!coef || !bands) goto done;
    for (size_t i = 0; i < npix; ++i) coef[i] = px[i];

    int levels = wavelet_levels(w, h);
    int nb = wavelet_forward(coef, w, h, levels, bands);
    if (nb <= 0) { rc = -4; goto done; }
    st = (QBandStat*)calloc((size_t)nb, sizeof(QBandStat));
    if (!st) goto done;
    if (quant_analyze(coef, w, bands, nb, (uint32_t)base_q16, st) != 0) { rc = -5; goto done; }
    if (quant_check_steps(st, nb) != 0) { rc = -6; goto done; }

    int lossless = 1;
    for (int b = 0; b < nb; ++b) if (!st[b].constant) lossless = 0;

    /* 组装未压缩负载 */
    size_t rawcap = npix * 6u + 256u + (size_t)nb * 32u;
    raw = (uint8_t*)malloc(rawcap);
    if (!raw) goto done;
    uint8_t *wp = raw, *wend = raw + rawcap;
    put_uvar(&wp, wend, (uint64_t)nb);
    for (int b = 0; b < nb; ++b) {
        put_uvar(&wp, wend, (uint64_t)st[b].constant);
        put_uvar(&wp, wend, zz_enc(st[b].mean));
        put_uvar(&wp, wend, (uint64_t)st[b].step_q16);
        if (st[b].constant) continue;       /* 常数带：只存均值，无损 */
        for (int y = 0; y < bands[b].h; ++y) {
            int32_t *row = coef + (bands[b].y + y) * w + bands[b].x;
            for (int x = 0; x < bands[b].w; ++x) {
                int32_t resid = row[x] - st[b].mean;
                put_uvar(&wp, wend, zz_enc(quant_enc(resid, st[b].step_q16)));
            }
        }
    }
    size_t rawlen = (size_t)(wp - raw);

    /* deflate（raw deflate，-15；level 9 追求最小体积） */
    uLongf zcap = compressBound((uLong)rawlen) + 16u;
    uint8_t *z = (uint8_t*)malloc(zcap);
    if (!z) goto done;
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (deflateInit2(&zs, 9, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK) {
        free(z); rc = -7; goto done;
    }
    zs.next_in = raw;
    zs.avail_in = (uInt)rawlen;
    zs.next_out = z;
    zs.avail_out = (uInt)zcap;
    int zr = deflate(&zs, Z_FINISH);
    uLong zlen = zs.total_out;
    deflateEnd(&zs);
    if (zr != Z_STREAM_END || zlen + SBC_HEADER_SIZE > cap) { free(z); rc = -8; goto done; }

    /* 16 字节定长头 */
    out[0] = SBC_MAGIC0; out[1] = SBC_MAGIC1;
    out[2] = SBC_MAGIC2; out[3] = SBC_MAGIC3;
    out[4] = (uint8_t)(w >> 8); out[5] = (uint8_t)w;
    out[6] = (uint8_t)(h >> 8); out[7] = (uint8_t)h;
    out[8] = (uint8_t)levels;
    out[9] = (uint8_t)q | (lossless ? 0x80u : 0u);
    out[10] = (uint8_t)(rawlen >> 24); out[11] = (uint8_t)(rawlen >> 16);
    out[12] = (uint8_t)(rawlen >> 8);  out[13] = (uint8_t)rawlen;
    out[14] = (uint8_t)(zlen >> 8);    out[15] = (uint8_t)zlen;
    memcpy(out + SBC_HEADER_SIZE, z, zlen);
    free(z);
    rc = (int)(SBC_HEADER_SIZE + zlen);
done:
    free(coef); free(bands); free(st); free(raw);
    return rc;
}

/* ---------------- 解码 ---------------- */
int sbc_probe(const uint8_t *data, size_t len, SbcInfo *info) {
    if (!data || len < SBC_HEADER_SIZE || !info) return -1;
    if (data[0] != SBC_MAGIC0 || data[1] != SBC_MAGIC1 ||
        data[2] != SBC_MAGIC2 || data[3] != SBC_MAGIC3)
        return -2;
    info->w = (uint16_t)((data[4] << 8) | data[5]);
    info->h = (uint16_t)((data[6] << 8) | data[7]);
    info->levels = data[8];
    info->quality = data[9] & 0x7fu;
    info->lossless = (data[9] & 0x80u) != 0;
    if (info->w == 0 || info->h == 0) return -3;
    if (info->levels < 1 || info->levels > 6) return -4;
    if (info->quality > 3) return -5;
    return 0;
}

int sbc_decode(const uint8_t *data, size_t len, uint8_t *px, size_t pxcap, SbcInfo *info) {
    SbcInfo inf;
    if (sbc_probe(data, len, &inf) != 0) return -2;
    if (info) *info = inf;
    if (!px) return -3;
    size_t npix = (size_t)inf.w * inf.h;
    if (npix > pxcap) return -4;

    uint32_t rawlen = ((uint32_t)data[10] << 24) | ((uint32_t)data[11] << 16) |
                      ((uint32_t)data[12] << 8)  | (uint32_t)data[13];
    uint32_t zlen = ((uint32_t)data[14] << 8) | data[15];
    if ((size_t)zlen + SBC_HEADER_SIZE != len) return -5;
    if (rawlen == 0 || rawlen > npix * 10u + 4096u) return -6;

    uint8_t *raw = (uint8_t*)malloc(rawlen);
    int32_t *coef = (int32_t*)calloc(npix, sizeof(int32_t));
    WBand *bands = (WBand*)malloc(25 * sizeof(WBand));
    int rc = -7;
    if (!raw || !coef || !bands) goto ddone;

    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -15) != Z_OK) goto ddone;
    zs.next_in = (Bytef*)(data + SBC_HEADER_SIZE);
    zs.avail_in = zlen;
    zs.next_out = raw;
    zs.avail_out = rawlen;
    int zr = inflate(&zs, Z_FINISH);
    inflateEnd(&zs);
    if (zr != Z_STREAM_END || zs.total_out != rawlen) { rc = -8; goto ddone; }

    const uint8_t *rp = raw, *rend = raw + rawlen;
    uint64_t u;
    if (get_uvar(&rp, rend, &u) != 0 || u == 0 || u > 64) { rc = -9; goto ddone; }
    int nb = (int)u;
    int expect = 1 + 3 * inf.levels;
    if (nb != expect) { rc = -10; goto ddone; }
    wavelet_layout(inf.w, inf.h, inf.levels, bands);

    for (int b = 0; b < nb; ++b) {
        uint64_t flag, zm, zstep;
        if (get_uvar(&rp, rend, &flag) != 0 || flag > 1) { rc = -11; goto ddone; }
        if (get_uvar(&rp, rend, &zm) != 0) { rc = -12; goto ddone; }
        if (get_uvar(&rp, rend, &zstep) != 0) { rc = -13; goto ddone; }
        /* 步长为零或负一律拒绝（编码值恒为正，越界即损坏/非法） */
        if (zstep == 0 || zstep > UINT32_MAX) { rc = -14; goto ddone; }
        int64_t mean = zz_dec(zm);
        if (mean < -32768 || mean > 32768) { rc = -15; goto ddone; }
        if (flag) {
            for (int y = 0; y < bands[b].h; ++y) {
                int32_t *row = coef + (bands[b].y + y) * inf.w + bands[b].x;
                for (int x = 0; x < bands[b].w; ++x) row[x] = (int32_t)mean;
            }
            continue;
        }
        for (int y = 0; y < bands[b].h; ++y) {
            int32_t *row = coef + (bands[b].y + y) * inf.w + bands[b].x;
            for (int x = 0; x < bands[b].w; ++x) {
                uint64_t zc;
                if (get_uvar(&rp, rend, &zc) != 0 || zc > UINT32_MAX) { rc = -16; goto ddone; }
                int32_t resid = quant_dec((int32_t)zz_dec(zc), (uint32_t)zstep);
                row[x] = (int32_t)mean + resid;
            }
        }
    }
    if (rp != rend) { rc = -17; goto ddone; }

    if (wavelet_inverse(coef, inf.w, inf.h, inf.levels) != 0) { rc = -18; goto ddone; }
    for (size_t i = 0; i < npix; ++i) {
        int64_t v = coef[i];
        if (v < 0) v = 0; else if (v > 255) v = 255;
        px[i] = (uint8_t)v;
    }
    rc = 0;
ddone:
    free(raw); free(coef); free(bands);
    return rc;
}
