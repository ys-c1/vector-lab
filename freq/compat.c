/* 兼容性闸门：锁定“原有空域滤波”的逐字节输出。
 *
 * 用法：
 *   compat           生成 run-out/ 下的 PGM 与 baseline/ 逐字节对比
 *   compat --write   重新生成 baseline/（仅在有意改动空域路径时使用）
 *
 * 输入是内置的确定性纹理（LCG，不依赖任何库），空域参数固定为
 * FQ_SPATIAL_SIGMAS 中的几档。新增频域逻辑后跑这个程序，任何空域输出的
 * 字节变化都会被抓出来。
 */
#include "freq.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned rng_state = 12345u;
static unsigned rng_u32(void) {
    rng_state = rng_state * 1664525u + 1013904223u;
    return rng_state;
}

static void synth(unsigned char *buf, int w, int h, int kind) {
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v;
            switch (kind) {
            case 0: v = ((((x % 4) < 2) ^ ((y % 4) < 2))) ? 216 : 40; break;
            case 1: v = (((x + y) % 4) < 2) ? 216 : 40; break;
            default: v = ((x % 8) < 4) ? 200 : 60; break;
            }
            buf[(size_t)y * w + x] = (unsigned char)v;
        }
}

static unsigned add_noise(unsigned char *buf, size_t n) {
    rng_state = 12345u;
    unsigned sum = 0;
    for (size_t i = 0; i < n; ++i) {
        int nz = (int)((rng_u32() >> 16) % 256u);
        int v = buf[i] + nz / 8 - 16;
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        buf[i] = (unsigned char)v;
        sum += (unsigned)buf[i];
    }
    return sum;
}

static int save_pgm(const char *path, const unsigned char *buf, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * h, f);
    fclose(f);
    return 0;
}

int main(int argc, char **argv) {
    int write_base = (argc > 1 && strcmp(argv[1], "--write") == 0);
    static const double sigmas[] = {0.0, 0.7, 1.0, 1.6, 2.5};
    static const int dims[][2] = {{128, 128}, {96, 80}, {80, 96}};
    static const int kinds[] = {0, 1, 2};

    char cmd[512];
    snprintf(cmd, sizeof cmd, "mkdir -p %s",
             write_base ? "baseline" : "run-out");
    if (system(cmd) != 0) return 1;

    int mismatches = 0, total = 0;
    for (unsigned d = 0; d < sizeof dims / sizeof dims[0]; ++d) {
        int w = dims[d][0], h = dims[d][1];
        unsigned char *in = (unsigned char*)malloc((size_t)w * h);
        unsigned char *out = (unsigned char*)malloc((size_t)w * h);
        if (!in || !out) return 2;
        for (unsigned k = 0; k < sizeof kinds / sizeof kinds[0]; ++k) {
            synth(in, w, h, kinds[k]);
            unsigned chk = add_noise(in, (size_t)w * h);
            for (unsigned s = 0; s < sizeof sigmas / sizeof sigmas[0]; ++s) {
                if (fq_filter_spatial(in, w, h, sigmas[s], out) != FQ_OK) return 3;
                char name[256], path[512];
                snprintf(name, sizeof name, "spatial_%dx%d_k%u_s%.1f.pgm",
                         w, h, k, sigmas[s]);
                if (write_base) {
                    snprintf(path, sizeof path, "baseline/%s", name);
                    if (save_pgm(path, out, w, h) != 0) return 4;
                    continue;
                }
                snprintf(path, sizeof path, "run-out/%s", name);
                if (save_pgm(path, out, w, h) != 0) return 4;

                char base[512];
                snprintf(base, sizeof base, "baseline/%s", name);
                unsigned chk2 = 0;
                for (size_t i = 0; i < (size_t)w * h; ++i) chk2 = chk2 * 131u + out[i];
                /* 与 baseline 文件比较（存在时） */
                FILE *bf = fopen(base, "rb");
                ++total;
                int same = 1;
                if (!bf) {
                    same = 0;
                } else {
                    char hdr[64];
                    int bw, bh, maxv;
                    if (fscanf(bf, "%63s %d %d %d", hdr, &bw, &bh, &maxv) != 4 ||
                        strcmp(hdr, "P5") != 0 || bw != w || bh != h || maxv != 255) {
                        same = 0;
                    } else {
                        int c = fgetc(bf);   /* 头部后的单个换行 */
                        (void)c;
                        for (size_t i = 0; i < (size_t)w * h && same; ++i) {
                            int v = fgetc(bf);
                            if (v != out[i]) same = 0;
                        }
                    }
                    fclose(bf);
                }
                if (!same) {
                    ++mismatches;
                    printf("MISMATCH %s (input sum %u output sum %u)\n", name, chk, chk2);
                }
            }
        }
        free(in); free(out);
    }

    if (write_base) {
        printf("baseline 已写入，共 %d 个文件\n", total);
        return 0;
    }
    printf("%d 个空域产物与 baseline 比对：%d 个不一致\n", total, mismatches);
    return mismatches ? 1 : 0;
}
