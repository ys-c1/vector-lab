/* 演示：生成覆盖四类内容的 PGM 测试集（确定性，无外部依赖）。
 * 用法：demo <outdir>
 *   solid.pgm    纯灰场（检验无损直通）
 *   shapes.pgm   多边形渲染风格的二元图（与 render 模块产物同类）
 *   photo.pgm    平滑渐变+混合高斯，模拟照片
 *   texture.pgm  高频噪声纹理（最难压缩）
 */
#include "src/pgm.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

static double rnd_state = 0x12345678u;
static double rnd(void) {
    rnd_state = fmod(rnd_state * 1103515245.0 + 12345.0, 2147483648.0);
    return rnd_state / 2147483648.0;
}

static void save(const char *dir, const char *name, const uint8_t *px, int w, int h) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (pgm_write_p5(path, px, w, h) != 0) { perror(path); exit(2); }
    printf("wrote %s (%dx%d)\n", path, w, h);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s outdir\n", argv[0]); return 1; }
    mkdir(argv[1], 0755);
    const int W = 256, H = 256, N = W * H;
    uint8_t *px = malloc(N);

    memset(px, 137, N);
    save(argv[1], "solid.pgm", px, W, H);

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int v = 0;
            if ((x - 80) * (x - 80) + (y - 90) * (y - 90) < 55 * 55) v = 255;
            int dx = x - 180, dy = y - 170;
            if (dx > -70 && dx < 70 && dy > -40 && dy < 40) v = 255;
            if ((x / 24 + y / 24) % 2 == 0 && x < 60 && y < 60) v = 255;
            px[y * W + x] = (uint8_t)v;
        }
    save(argv[1], "shapes.pgm", px, W, H);

    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double v = 128.0
                + 60.0 * sin(0.06 * x) * cos(0.05 * y)
                + 45.0 * exp(-((x - 190.0) * (x - 190.0) + (y - 70.0) * (y - 70.0)) / 1800.0)
                - 40.0 * exp(-((x - 70.0) * (x - 70.0) + (y - 190.0) * (y - 190.0)) / 2600.0);
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            px[y * W + x] = (uint8_t)v;
        }
    save(argv[1], "photo.pgm", px, W, H);

    /* 真实感细节图：低频底纹 + 定向边缘 + 轻微颗粒（非白噪声） */
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double v = 128.0
                + 35.0 * sin(0.08 * x + 0.05 * y)
                + 25.0 * cos(0.03 * y);
            double edge = sin(0.55 * x + 0.4 * sin(0.07 * y)); /* 定向纹理 */
            v += 28.0 * edge;
            double grain = (rnd() - 0.5) * 14.0;                 /* 轻颗粒 */
            v += grain;
            /* 局部高对比小块，模拟文字/细节区域 */
            if (x > 40 && x < 110 && y > 150 && y < 200 &&
                ((x + y) / 6) % 2 == 0) v += 60.0;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            px[y * W + x] = (uint8_t)v;
        }
    save(argv[1], "texture.pgm", px, W, H);

    free(px);
    return 0;
}
