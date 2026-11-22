/* pgmdiff：两张同尺寸 P5 PGM 逐像素比对。
 *   pgmdiff <a.pgm> <b.pgm> [overlay.pgm]
 * 输出首个差异字节偏移、差异像素数、总像素数；
 * 给了 overlay 时写一张同尺寸 PGM：相等像素=黑，差异像素=|a-b|。
 * 流式分块处理，工作内存固定 4KB*2，与图像尺寸无关。
 * 退出码：0 完全一致；1 有差异；2 用法/文件/格式错误。 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHUNK 4096

static int read_token(FILE *f, char *tok, size_t cap) {
    int c;
    do {
        c = fgetc(f);
        if (c == EOF) return -1;
        if (c == '#') { /* 跳过注释行 */
            while (c != '\n' && c != EOF) c = fgetc(f);
            if (c == EOF) return -1;
            c = fgetc(f);
        }
    } while (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '#');
    size_t n = 0;
    while (c != ' ' && c != '\t' && c != '\n' && c != '\r' && c != EOF) {
        if (n + 1 >= cap) return -1;
        tok[n++] = (char)c;
        c = fgetc(f);
    }
    tok[n] = 0;
    return 0;
}

static int read_header(FILE *f, int *w, int *h, int *maxv) {
    char tok[32];
    if (read_token(f, tok, sizeof tok) != 0 || strcmp(tok, "P5") != 0)
        return -1;
    if (read_token(f, tok, sizeof tok) != 0) return -1;
    *w = atoi(tok);
    if (read_token(f, tok, sizeof tok) != 0) return -1;
    *h = atoi(tok);
    if (read_token(f, tok, sizeof tok) != 0) return -1;
    *maxv = atoi(tok);
    /* read_token 已吃掉 maxval 后面的分隔空白，下一个字节即像素数据。 */
    if (*w <= 0 || *h <= 0 || *maxv != 255) return -1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        printf("usage: %s <a.pgm> <b.pgm> [overlay.pgm]\n", argv[0]);
        return 2;
    }
    FILE *fa = fopen(argv[1], "rb");
    FILE *fb = fopen(argv[2], "rb");
    if (!fa || !fb) {
        printf("error: cannot open input files\n");
        return 2;
    }
    int wa, ha, ma, wb, hb, mb;
    if (read_header(fa, &wa, &ha, &ma) != 0 ||
        read_header(fb, &wb, &hb, &mb) != 0) {
        printf("error: invalid PGM (only binary P5 maxval 255 supported)\n");
        return 2;
    }
    if (wa != wb || ha != hb) {
        printf("error: size mismatch: %s=%dx%d vs %s=%dx%d\n",
               argv[1], wa, ha, argv[2], wb, hb);
        return 2;
    }
    FILE *fo = NULL;
    if (argc >= 4) {
        fo = fopen(argv[3], "wb");
        if (!fo) {
            printf("error: cannot open overlay %s\n", argv[3]);
            return 2;
        }
        fprintf(fo, "P5\n%d %d\n255\n", wa, ha);
    }

    size_t total = (size_t)wa * (size_t)ha;
    unsigned char ba[CHUNK], bb[CHUNK], bd[CHUNK];
    size_t done = 0, diffpx = 0;
    long firstoff = -1; /* 含 PGM 头的文件字节偏移 */
    long header_bytes = ftell(fa);
    while (done < total) {
        size_t want = total - done;
        if (want > CHUNK) want = CHUNK;
        size_t na = fread(ba, 1, want, fa);
        size_t nb = fread(bb, 1, want, fb);
        if (na != want || nb != want) {
            printf("error: unexpected end of pixel data\n");
            if (fo) fclose(fo);
            return 2;
        }
        for (size_t i = 0; i < want; ++i) {
            if (ba[i] != bb[i]) {
                if (firstoff < 0)
                    firstoff = header_bytes + (long)(done + i) + 1;
                ++diffpx;
                bd[i] = (unsigned char)(ba[i] > bb[i] ? ba[i] - bb[i]
                                                       : bb[i] - ba[i]);
            } else {
                bd[i] = 0;
            }
        }
        if (fo) fwrite(bd, 1, want, fo);
        done += want;
    }
    fclose(fa);
    fclose(fb);
    if (fo) fclose(fo);

    if (diffpx == 0) {
        printf("IDENTICAL %s %s pixels %zu diff_pixels 0\n",
               argv[1], argv[2], total);
        return 0;
    }
    printf("DIFF %s %s first_byte %ld diff_pixels %zu total_pixels %zu\n",
           argv[1], argv[2], firstoff, diffpx, total);
    if (fo) printf("overlay %s\n", argv[3]);
    return 1;
}
