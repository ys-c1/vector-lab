/* 旧逻辑（单层凸包）演示，新逻辑关闭时使用。
 * 与改造前的 hull_convex 行为一致，并把单层轮廓栅格化成 demo.pgm。
 * 该 PGM 是逐字节稳定性基线：旧逻辑路径产出必须与已提交基线相同。 */
#include "hull.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define W 80
#define H 80
#define MARGIN 0.05

static int inside(const HPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->count; ++i) {
        HPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

int main(int argc, char **argv) {
    HPoint p[6] = {{0,0},{5,0},{5,4},{2,1},{0,4},{3,2}};
    HPoly out;
    if (hull_convex(p, 6, &out) != 0) return 1;
    printf("hull %d\n", out.count);
    for (int i = 0; i < out.count; ++i)
        printf("  %.2f %.2f\n", out.pts[i].x, out.pts[i].y);

    const char *path = argc >= 2 ? argv[1] : "demo.pgm";
    HBox box;
    if (hull_box(p, 6, MARGIN, &box) != 0) return 2;
    double dx = (box.maxx - box.minx) / W, dy = (box.maxy - box.miny) / H;
    unsigned char *buf = (unsigned char*)calloc((size_t)W * H, 1);
    if (!buf) return 3;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double px = box.minx + (x + 0.5) * dx;
            double py = box.miny + (y + 0.5) * dy;
            if (inside(&out, px, py)) buf[y * W + x] = 255;
        }
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); free(buf); return 4; }
    fprintf(f, "P5\n%d %d\n255\n", W, H);
    fwrite(buf, 1, (size_t)W * H, f);
    fclose(f);
    free(buf);
    printf("pgm %s\n", path);
    return 0;
}
