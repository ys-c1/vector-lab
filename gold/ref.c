/* 独立参照实现：与主代码不同算法，只负责产出可比对的文件。
 * 用法：
 *   ref area  <poly.txt>          输出面积，6 位小数
 *   ref pixels <poly.txt> <w> <h> 输出 PGM，8x8 超采样
 *   ref points <line.txt> <n>     输出 n 等分点（按累计弧长）
 * 输入格式：每行 "x y"，多边形首尾自动闭合。
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAXP 512
typedef struct { double x, y; } P;

static int load(const char *path, P *out) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int n = 0;
    while (n < MAXP && fscanf(f, "%lf %lf", &out[n].x, &out[n].y) == 2) ++n;
    fclose(f);
    return n;
}

static int inside(const P *p, int n, double x, double y) {
    int c = 0;
    for (int i = 0; i < n; ++i) {
        P a = p[i], b = p[(i + 1) % n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static int cmd_area(const char *path) {
    P p[MAXP];
    int n = load(path, p);
    if (n < 3) return 2;
    double minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; ++i) {
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
    }
    const int SS = 256;
    long hit = 0;
    double dx = (maxx - minx) / SS, dy = (maxy - miny) / SS;
    for (int j = 0; j < SS; ++j)
        for (int i = 0; i < SS; ++i)
            if (inside(p, n, minx + (i + 0.5) * dx, miny + (j + 0.5) * dy)) ++hit;
    double area = (double)hit / (double)(SS * SS) * (maxx - minx) * (maxy - miny);
    printf("%.6f\n", area);
    return 0;
}

static int cmd_pixels(const char *path, int w, int h) {
    /* 当场停并指名参数：宽或高为零/负。 */
    if (w <= 0) {
        fprintf(stderr, "error: width must be positive, got %d (param: width)\n", w);
        return 2;
    }
    if (h <= 0) {
        fprintf(stderr, "error: height must be positive, got %d (param: height)\n", h);
        return 2;
    }
    P p[MAXP];
    int n = load(path, p);
    if (n < 3) {
        fprintf(stderr, "error: polygon needs at least 3 points (param: poly)\n");
        return 2;
    }
    double minx = p[0].x, maxx = p[0].x, miny = p[0].y, maxy = p[0].y;
    for (int i = 1; i < n; ++i) {
        if (p[i].x < minx) minx = p[i].x;
        if (p[i].x > maxx) maxx = p[i].x;
        if (p[i].y < miny) miny = p[i].y;
        if (p[i].y > maxy) maxy = p[i].y;
    }
    double sx = (maxx - minx) / w, sy = (maxy - miny) / h;
    unsigned char *buf = (unsigned char*)calloc((size_t)w * h, 1);
    if (!buf) return 3;
    const int SS = 8;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int c = 0;
            for (int j = 0; j < SS; ++j)
                for (int i = 0; i < SS; ++i) {
                    double px = minx + (x + (i + 0.5) / SS) * sx;
                    double py = miny + (y + (j + 0.5) / SS) * sy;
                    if (inside(p, n, px, py)) ++c;
                }
            buf[y * w + x] = (unsigned char)(255 * c / (SS * SS));
        }
    printf("P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * h, stdout);
    free(buf);
    return 0;
}

static int cmd_points(const char *path, int n) {
    P p[MAXP];
    int k = load(path, p);
    if (k < 2 || n < 2) return 2;
    double total = 0.0;
    for (int i = 0; i + 1 < k; ++i) {
        double dx = p[i+1].x - p[i].x, dy = p[i+1].y - p[i].y;
        total += sqrt(dx*dx + dy*dy);
    }
    double step = total / (n - 1);
    printf("%d\n", n);
    for (int s = 0; s < n; ++s) {
        double target = step * s, acc = 0.0;
        int seg = 0;
        for (; seg + 1 < k - 1; ++seg) {
            double dx = p[seg+1].x - p[seg].x, dy = p[seg+1].y - p[seg].y;
            double len = sqrt(dx*dx + dy*dy);
            if (acc + len >= target) break;
            acc += len;
        }
        double dx = p[seg+1].x - p[seg].x, dy = p[seg+1].y - p[seg].y;
        double len = sqrt(dx*dx + dy*dy);
        double f = len > 1e-12 ? (target - acc) / len : 0.0;
        printf("%.6f %.6f\n", p[seg].x + dx * f, p[seg].y + dy * f);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    if (!strcmp(argv[1], "area")) return cmd_area(argv[2]);
    if (!strcmp(argv[1], "pixels") && argc >= 5) return cmd_pixels(argv[2], atoi(argv[3]), atoi(argv[4]));
    if (!strcmp(argv[1], "points") && argc >= 4) return cmd_points(argv[2], atoi(argv[3]));
    return 1;
}
