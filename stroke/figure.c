/* 固定宽度描边的位图产物：与新版面积反解无关，只用 stroke.h 的 SPoint。
 * 像素中心到中心线距离 <= width/2 即算覆盖（圆头胶囊并集），无抗锯齿，
 * 因此输出逐字节确定。
 *
 * 用法: ./build/figure out.pgm [width]
 * 默认宽度 3.0 —— 与 demo.c 的固定宽度一致，作为“原来的固定宽度”产物。
 *
 * 回归：新逻辑（STROKE_AREA_FIT）默认关闭时，本程序编出的 PGM 必须与
 * baseline/figure.pgm 逐字节相同（make regress 核对）。 */
#include "stroke.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct { int n; SPoint pts[8]; } Path;

/* 四组路径，按 2x2 摆放，与 demo_area.c 相同的几何。 */
static const Path paths[4] = {
    { 3, {{0,0},{30,0},{30,20}} },
    { 4, {{0,0},{40,0},{40,20},{0,20}} },
    { 4, {{0,0},{30,20},{30,0},{0,20}} },
    { 6, {{0,0},{50,0},{50,20},{20,20},{20,50},{0,50}} },
};
static const int closed[4] = { 0, 1, 0, 1 };

#define W 320
#define H 240
#define CELL_W 160
#define CELL_H 120
#define SCALE 1.4
#define PAD 8

static int covered(const Path *p, int is_closed, double x, double y, double h) {
    int m = is_closed ? p->n : p->n - 1;
    for (int i = 0; i < m; ++i) {
        SPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        double dx = b.x - a.x, dy = b.y - a.y;
        double l2 = dx * dx + dy * dy;
        double t = l2 > 0.0 ? ((x - a.x) * dx + (y - a.y) * dy) / l2 : 0.0;
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        double ex = x - (a.x + dx * t), ey = y - (a.y + dy * t);
        if (ex * ex + ey * ey <= h * h) return 1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *out = argc > 1 ? argv[1] : "figure.pgm";
    double width = argc > 2 ? atof(argv[2]) : 3.0;
    if (!(width > 0.0)) { fprintf(stderr, "width must be > 0\n"); return 2; }
    double h = width * 0.5;

    unsigned char *buf = (unsigned char *)calloc((size_t)W * H, 1);
    if (!buf) return 3;
    long on = 0;
    for (int k = 0; k < 4; ++k) {
        int cx = (k % 2) * CELL_W;
        int cy = (k / 2) * CELL_H;
        for (int py = 0; py < CELL_H; ++py)
            for (int px = 0; px < CELL_W; ++px) {
                double x = (px - cx - PAD) / SCALE;
                double y = (CELL_H - PAD - py + cy) / SCALE;
                int hit = 0;
                for (int q = 0; q < 4 && !hit; ++q)
                    if (q == k && covered(&paths[q], closed[q], x, y, h)) hit = 1;
                if (hit) { buf[(size_t)(cy + py) * W + cx + px] = 255; ++on; }
            }
    }
    FILE *f = fopen(out, "wb");
    if (!f) { free(buf); return 4; }
    fprintf(f, "P5\n%d %d\n255\n", W, H);
    fwrite(buf, 1, (size_t)W * H, f);
    fclose(f);
    printf("figure %s width %.3f on %ld / %d\n", out, width, on, W * H);
    free(buf);
    return 0;
}
