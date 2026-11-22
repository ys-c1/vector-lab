#include "src/pgm.h"
#include "src/sbc.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static double median3(double a[3]) {
    double t[3] = {a[0], a[1], a[2]};
    for (int i = 0; i < 2; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (t[j] < t[i]) { double x = t[i]; t[i] = t[j]; t[j] = x; }
    return t[1];
}

static int parse_quality(const char *s, QPreset *q) {
    if (!strcmp(s, "high"))     { *q = Q_HIGH;     return 0; }
    if (!strcmp(s, "balanced")) { *q = Q_BALANCED; return 0; }
    if (!strcmp(s, "small"))    { *q = Q_SMALL;    return 0; }
    if (!strcmp(s, "tiny"))     { *q = Q_TINY;     return 0; }
    return -1;
}

static long file_size(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    fclose(f);
    return n;
}

static int cmd_enc(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s enc in.pgm out.sbc {high|balanced|small|tiny}\n", argv[0]);
        return 1;
    }
    QPreset q;
    if (parse_quality(argv[4], &q) != 0) { fprintf(stderr, "bad quality\n"); return 2; }
    uint8_t *px; int w, h;
    if (pgm_read(argv[2], &px, &w, &h) != 0) { fprintf(stderr, "read pgm failed\n"); return 3; }
    size_t cap = sbc_max_size(w, h);
    uint8_t *buf = (uint8_t*)malloc(cap);
    if (!buf) { free(px); return 4; }
    int n = sbc_encode(px, w, h, q, buf, cap);
    if (n < 0) { fprintf(stderr, "encode failed %d\n", n); free(buf); free(px); return 5; }
    FILE *f = fopen(argv[3], "wb");
    if (!f) { free(buf); free(px); return 6; }
    if (fwrite(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf); free(px); return 7; }
    fclose(f);
    long in_sz = file_size(argv[2]);
    printf("enc %s -> %s quality=%s %dx%d raw=%ld comp=%d ratio=%.1f%%\n",
           argv[2], argv[3], quant_preset_name(q), w, h, in_sz, n,
           in_sz > 0 ? 100.0 * n / in_sz : 0.0);
    free(buf); free(px);
    return 0;
}

static int cmd_dec(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s dec in.sbc out.pgm\n", argv[0]);
        return 1;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f) { fprintf(stderr, "open sbc failed\n"); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t*)malloc((size_t)n);
    if (!data || fread(data, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(data); return 3; }
    fclose(f);
    SbcInfo info;
    if (sbc_probe(data, (size_t)n, &info) != 0) { fprintf(stderr, "bad sbc header\n"); free(data); return 4; }
    uint8_t *px = (uint8_t*)malloc((size_t)info.w * info.h);
    if (!px) { free(data); return 5; }
    int rc = sbc_decode(data, (size_t)n, px, (size_t)info.w * info.h, &info);
    if (rc != 0) { fprintf(stderr, "decode rejected rc=%d\n", rc); free(px); free(data); return 6; }
    if (pgm_write_p5(argv[3], px, info.w, info.h) != 0) { fprintf(stderr, "write pgm failed\n"); free(px); free(data); return 7; }
    printf("dec %s -> %s %dx%d quality=%s%s\n", argv[2], argv[3], info.w, info.h,
           quant_preset_name((QPreset)info.quality), info.lossless ? " lossless=1" : "");
    free(px); free(data);
    return 0;
}

static int cmd_info(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s info in.sbc\n", argv[0]); return 1; }
    FILE *f = fopen(argv[2], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t data[SBC_HEADER_SIZE];
    if (fread(data, 1, SBC_HEADER_SIZE, f) != SBC_HEADER_SIZE) { fclose(f); return 3; }
    fclose(f);
    SbcInfo info;
    if (sbc_probe(data, (size_t)n, &info) != 0) { fprintf(stderr, "bad sbc\n"); return 4; }
    printf("file=%s bytes=%ld dims=%ux%u levels=%u quality=%s lossless=%d\n",
           argv[2], n, info.w, info.h, info.levels,
           quant_preset_name((QPreset)info.quality), info.lossless);
    return 0;
}

/* bench in.pgm：四档各跑 3 次取中位耗时，按硬门槛判定。 */
static int cmd_bench(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s bench in.pgm [in2.pgm ...]\n", argv[0]); return 1; }
    static const QPreset qs[4] = {Q_HIGH, Q_BALANCED, Q_SMALL, Q_TINY};
    int any_fail = 0;
    for (int fi = 2; fi < argc; ++fi) {
        uint8_t *px; int w, h;
        if (pgm_read(argv[fi], &px, &w, &h) != 0) { fprintf(stderr, "skip %s\n", argv[fi]); continue; }
        size_t npix = (size_t)w * h;
        long pgm_sz = file_size(argv[fi]);
        size_t cap = sbc_max_size(w, h);
        uint8_t *cbuf = (uint8_t*)malloc(cap);
        uint8_t *dec = (uint8_t*)malloc(npix);
        if (!cbuf || !dec) { free(cbuf); free(dec); free(px); return 5; }
        printf("== %s  %dx%d  pgm=%ld bytes (pixel data %zu) ==\n",
               argv[fi], w, h, pgm_sz, npix);
        printf("%-9s %10s %9s %8s %9s %9s %s\n",
               "quality", "bytes", "ratio", "PSNR/dB", "enc/ms", "dec/ms", "verdict");
        for (int k = 0; k < 4; ++k) {
            int n = sbc_encode(px, w, h, qs[k], cbuf, cap);
            if (n < 0) { fprintf(stderr, "encode fail %d\n", n); any_fail = 1; continue; }
            double te[3], td[3];
            SbcInfo info;
            for (int r = 0; r < 3; ++r) {
                double t0 = now_ms();
                int nr = sbc_encode(px, w, h, qs[k], cbuf, cap);
                double t1 = now_ms();
                if (nr < 0) { fprintf(stderr, "encode fail\n"); any_fail = 1; }
                te[r] = t1 - t0;
                double t2 = now_ms();
                int dr = sbc_decode(cbuf, (size_t)n, dec, npix, &info);
                double t3 = now_ms();
                if (dr != 0) { fprintf(stderr, "decode rejected\n"); any_fail = 1; }
                td[r] = t3 - t2;
            }
            double psnr = sbc_psnr(px, dec, npix);
            double ratio = 100.0 * n / pgm_sz;
            int size_ok = ratio <= 60.0;           /* 体积比现在小 ≥40% */
            int psnr_ok = psnr >= 32.0;            /* PSNR 不低于 32 dB */
            int pass = size_ok && psnr_ok;
            if (!pass) any_fail = 1;
            printf("%-9s %10d %8.1f%% %8.2f %9.3f %9.3f %s%s\n",
                   quant_preset_name(qs[k]), n, ratio, psnr,
                   median3(te), median3(td), pass ? "PASS" : "FAIL",
                   pass ? "" : (!size_ok ? " (size)" : " (psnr)"));
        }
        free(cbuf); free(dec); free(px);
    }
    return any_fail ? 8 : 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "subband codec\n"
                "  %s enc in.pgm out.sbc {high|balanced|small|tiny}\n"
                "  %s dec in.sbc out.pgm\n"
                "  %s info in.sbc\n"
                "  %s bench in.pgm [in2.pgm ...]\n",
                argv[0], argv[0], argv[0], argv[0]);
        return 1;
    }
    if (!strcmp(argv[1], "enc"))   return cmd_enc(argc, argv);
    if (!strcmp(argv[1], "dec"))   return cmd_dec(argc, argv);
    if (!strcmp(argv[1], "info"))  return cmd_info(argc, argv);
    if (!strcmp(argv[1], "bench")) return cmd_bench(argc, argv);
    fprintf(stderr, "unknown subcommand\n");
    return 1;
}
