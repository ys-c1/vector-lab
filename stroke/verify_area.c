/* 面积反解的独立核验：用逐像素光栅计数（与库内逐行并集、二次模型、
 * 蒙特卡洛都不同的一条路径）量出反解宽度下的覆盖面积，与目标比对。
 *
 * 用法: ./build/verify <target> [w_min] [w_max]
 * 四组路径与 demo_area.c 相同；每组输出反解宽度、库内面积、光栅面积与
 * 相对偏差，任一偏差超过 1% 则以非零码退出。
 */
#include "stroke_area.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 800
#define H 800

static int covered(const SPoint *p, int n, int closed, double x, double y, double h) {
    int m = closed ? n : n - 1;
    for (int i = 0; i < m; ++i) {
        SPoint a = p[i], b = p[(i + 1) % n];
        double dx = b.x - a.x, dy = b.y - a.y;
        double l2 = dx * dx + dy * dy;
        double t = l2 > 0.0 ? ((x - a.x) * dx + (y - a.y) * dy) / l2 : 0.0;
        if (t < 0.0) t = 0.0; else if (t > 1.0) t = 1.0;
        double ex = x - (a.x + dx * t), ey = y - (a.y + dy * t);
        if (ex * ex + ey * ey <= h * h) return 1;
    }
    return 0;
}

static double raster_area(const SPoint *p, int n, int closed, double width) {
    double h = width * 0.5;
    double minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; ++i) {
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
    }
    minx -= h; maxx += h; miny -= h; maxy += h;
    double span_x = maxx - minx, span_y = maxy - miny;
    /* 窄带必须横跨足够多的像素，否则光栅计数本身就失去精度。
     * 每半个宽度至少 96 像素，上限 4096 宽/高。 */
    int gw = W, gh = H;
    double need_x = ceil(96.0 * span_x / h), need_y = ceil(96.0 * span_y / h);
    if (need_x > gw) gw = (int)need_x;
    if (need_y > gh) gh = (int)need_y;
    if (gw > 4096) gw = 4096;
    if (gh > 4096) gh = 4096;
    double sx = span_x / gw, sy = span_y / gh;
    long on = 0;
    for (int y = 0; y < gh; ++y)
        for (int x = 0; x < gw; ++x)
            if (covered(p, n, closed, minx + (x + 0.5) * sx, miny + (y + 0.5) * sy, h))
                ++on;
    return (double)on * sx * sy;
}

int main(int argc, char **argv) {
    double target = argc > 1 ? atof(argv[1]) : 1200.0;
    double wmin = argc > 2 ? atof(argv[2]) : 0.01;
    double wmax = argc > 3 ? atof(argv[3]) : 400.0;

    SPoint paths[4][6];
    int np[4], cl[4];
    SPoint t0[3] = {{0,0},{30,0},{30,20}};
    SPoint t1[4] = {{0,0},{40,0},{40,20},{0,20}};
    SPoint t2[4] = {{0,0},{30,20},{30,0},{0,20}};
    SPoint t3[6] = {{0,0},{50,0},{50,20},{20,20},{20,50},{0,50}};
    memcpy(paths[0], t0, sizeof t0); np[0] = 3; cl[0] = 0;
    memcpy(paths[1], t1, sizeof t1); np[1] = 4; cl[1] = 1;
    memcpy(paths[2], t2, sizeof t2); np[2] = 4; cl[2] = 0;
    memcpy(paths[3], t3, sizeof t3); np[3] = 6; cl[3] = 1;

    int bad = 0;
    printf("target %.4f, raster %dx%d\n", target, W, H);
    for (int k = 0; k < 4; ++k) {
        SAreaFit f;
        int rc = stroke_area_fit(paths[k], np[k], cl[k], target, wmin, wmax, &f);
        if (rc != STROKE_AREA_OK) {
            printf("case %d rejected: %s\n", k + 1, stroke_area_strerror(rc));
            ++bad;
            continue;
        }
        double r = raster_area(paths[k], np[k], cl[k], f.width);
        double rel = fabs(r - target) / target;
        double lib = f.rel_err;
        /* 光栅计数本身在极窄带（半像素量级）有离散误差；这里是独立核验，
         * 容差取 1% 与目标一致，更窄的带以库内自适应积分为准。 */
        printf("case %d w=%.6f lib_area=%.4f (%.4f%%) raster=%.4f (%.4f%%)%s\n",
               k + 1, f.width, f.coverage, lib * 100.0, r, rel * 100.0,
               (lib <= 0.01 && rel <= 0.01) ? "" : "  <-- FAIL");
        if (lib > 0.01 || rel > 0.01) ++bad;
    }
    printf("%s\n", bad ? "VERIFY FAIL" : "VERIFY OK");
    return bad ? 1 : 0;
}
