#include "src/rdq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 读 P5 PGM（maxval 255）。返回 0 成功，负值为错误。 */
static int read_pgm(const char *path, unsigned char **out, int *w, int *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char magic[3] = {0};
    if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P5") != 0) { fclose(f); return -2; }
    int W = 0, H = 0, maxv = 0;
    if (fscanf(f, "%d %d %d", &W, &H, &maxv) != 3 || W <= 0 || H <= 0 || maxv != 255) {
        fclose(f);
        return -2;
    }
    /* 头后恰好一个空白字符（规范里可能是 \n 或 \r\n） */
    int c = fgetc(f);
    if (c == '\r') c = fgetc(f);
    if (c == EOF) { fclose(f); return -2; }
    unsigned char *buf = (unsigned char*)malloc((size_t)W * H);
    if (!buf) { fclose(f); return -3; }
    if (fread(buf, 1, (size_t)W * H, f) != (size_t)W * H) {
        free(buf);
        fclose(f);
        return -2;
    }
    fclose(f);
    *out = buf;
    *w = W;
    *h = H;
    return 0;
}

static int write_pgm(const char *path, const unsigned char *buf, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    return 0;
}

static int cmd_encode(const char *in, const char *out, long budget, int block) {
    unsigned char *img = 0;
    int w = 0, h = 0;
    int rc = read_pgm(in, &img, &w, &h);
    if (rc != 0) { fprintf(stderr, "encode: read %s failed (%d)\n", in, rc); return 2; }

    RQParams p = {w, h, block, 1, RQ_DEV_BITS};
    RQStats st;
    const char *bad = 0;
    RQImage q;
    rc = rq_encode(img, &p, budget, &q, &st, &bad);
    if (rc != 0) {
        fprintf(stderr, "encode: rejected (%d), parameter: %s\n", rc, bad ? bad : "?");
        free(img);
        return rc == -2 ? 3 : 4;
    }
    unsigned char *buf = (unsigned char*)malloc((size_t)q.nbytes);
    if (!buf) { rq_free(&q); free(img); return 5; }
    const int n = rq_serialize(&q, buf, (size_t)q.nbytes);
    if (n != q.nbytes) { fprintf(stderr, "encode: serialize mismatch\n"); free(buf); rq_free(&q); free(img); return 5; }
    FILE *f = fopen(out, "wb");
    if (!f) { fprintf(stderr, "encode: open %s failed\n", out); free(buf); rq_free(&q); free(img); return 2; }
    fwrite(buf, 1, (size_t)n, f);
    fclose(f);
    printf("block %d  bytes %d / budget %ld (%.1f%%)  PSNR %.3f dB  lambda %.4g\n",
           block, st.nbytes, budget, 100.0 * (double)st.nbytes / (double)budget,
           st.psnr, st.lambda);
    free(buf); rq_free(&q); free(img);
    return 0;
}

static int cmd_decode(const char *in, const char *out) {
    FILE *f = fopen(in, "rb");
    if (!f) { fprintf(stderr, "decode: open %s failed\n", in); return 2; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0) { fclose(f); return 2; }
    unsigned char *buf = (unsigned char*)malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); fclose(f); return 2; }
    fclose(f);
    RQImage q;
    int rc = rq_deserialize(buf, (size_t)n, &q);
    free(buf);
    if (rc != 0) { fprintf(stderr, "decode: bad stream (%d)\n", rc); return 3; }
    unsigned char *img = (unsigned char*)malloc((size_t)q.w * q.h);
    if (!img) { rq_free(&q); return 5; }
    if (rq_decode(&q, img) != 0) { free(img); rq_free(&q); return 3; }
    rc = write_pgm(out, img, q.w, q.h);
    free(img);
    rq_free(&q);
    if (rc != 0) { fprintf(stderr, "decode: write %s failed\n", out); return 2; }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
                "usage:\n"
                "  %s encode in.pgm out.rq budget [block]\n"
                "  %s decode in.rq out.pgm\n",
                argv[0], argv[0]);
        return 1;
    }
    if (!strcmp(argv[1], "encode") && argc >= 5) {
        long budget = atol(argv[4]);
        int block = argc >= 6 ? atoi(argv[5]) : 16;
        if (block <= 0) block = 16;
        return cmd_encode(argv[2], argv[3], budget, block);
    }
    if (!strcmp(argv[1], "decode") && argc >= 4) return cmd_decode(argv[2], argv[3]);
    fprintf(stderr, "bad arguments\n");
    return 1;
}
