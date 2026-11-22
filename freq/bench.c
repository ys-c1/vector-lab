/* 耗时分解：每个阶段单独计时，四类纹理 + 三种尺寸。
 * 列：图 | 尺寸 | 准备(ms) | 正变换 | 选截止 | 逆变换+组装 | 总耗时
 * 准备含反射填充与去均值；选截止含环带统计。
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

static void synth(unsigned char *buf, int w, int h, int kind, int noise) {
    rng_state = 12345u;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int v;
            switch (kind) {
            case 0: v = ((x % 4) < 2) ? 216 : 40; break;
            case 1: v = ((((x % 4) < 2) ^ ((y % 4) < 2))) ? 216 : 40; break;
            case 2: v = (((x + y) % 4) < 2) ? 216 : 40; break;
            default: {
                int dx2 = 2 * (x % 8) - 7, dy2 = 2 * (y % 8) - 7;
                v = (dx2 * dx2 + dy2 * dy2 <= 24) ? 216 : 40;
                break;
            }
            }
            if (noise) {
                int n = (int)((rng_u32() >> 16) % 256u);
                n = (n * 24) / 256 - 12;
                v += n;
            }
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            buf[(size_t)y * w + x] = (unsigned char)v;
        }
}

int main(void) {
    const char *names[4] = {"stripes", "checker", "diag", "dots"};
    const int dims[][2] = {{128, 128}, {256, 256}, {512, 512}};
    FqOptions opt;
    fq_options_default(&opt);

    printf("| 纹理 | 尺寸 | 准备 ms | 正变换 ms | 选截止 ms | 逆变换 ms | 组装 ms | 总 ms |\n");
    printf("|---|---:|---:|---:|---:|---:|---:|---:|\n");
    for (unsigned d = 0; d < sizeof dims / sizeof dims[0]; ++d) {
        int w = dims[d][0], h = dims[d][1];
        unsigned char *in = (unsigned char*)malloc((size_t)w * h);
        unsigned char *out = (unsigned char*)malloc((size_t)w * h);
        if (!in || !out) return 1;
        for (int k = 0; k < 4; ++k) {
            synth(in, w, h, k, 1);
            FqReport rep;
            int rc = fq_run(in, w, h, &opt, out, &rep);
            if (rc != FQ_OK) {
                printf("| %s | %dx%d | 失败 rc=%d | | | | | |\n", names[k], w, h, rc);
                continue;
            }
            printf("| %s | %dx%d | %.2f | %.2f | %.2f | %.2f | %.2f | %.2f |\n",
                   names[k], w, h, rep.ms_pad, rep.ms_forward, rep.ms_select,
                   rep.ms_inverse, rep.ms_assemble, rep.ms_total);
        }
        free(in); free(out);
    }
    return 0;
}
