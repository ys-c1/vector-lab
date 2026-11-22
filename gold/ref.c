/* 独立参照实现：与主代码不同算法，只负责产出可比对的文件。
 * 用法：
 *   ref area   <poly.txt>          输出面积，6 位小数
 *   ref pixels <poly.txt> <w> <h>  输出 PGM，8x8 超采样
 *   ref points <line.txt> <n>      输出 n 等分点（按累计弧长）
 *   ref fill   <path.d> <w> <h> [ss] [auto|nonzero|evenodd] [out.pgm]
 *                                  解析 SVG 路径填充，扫描线算法
 * 输入格式：poly/line 每行 "x y"，多边形首尾自动闭合。
 *
 * fill 的算法刻意与主模块不同：主模块对每个采样点做一次射线计数，
 * 参照先把一整条扫描线上的交点收集、排序，再按区间累加绕数，最后
 * 用区间覆盖判断采样点是否填充。规则推导也独立重算一遍符号面积。
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
    P p[MAXP];
    int n = load(path, p);
    if (n < 3 || w <= 0 || h <= 0) return 2;
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

/* ---------------- fill：独立的 SVG 子路径解析 + 扫描线 --------------- */

#define FMAXS 128
typedef struct { int nsub; int first[FMAXS]; int cnt[FMAXS]; P v[2048]; } FPath;
/* v[] stores the vertex list; subpath s uses v[first[s] .. first[s]+cnt[s]-1].
 * Each subpath is implicitly closed. */

static const char *fskip(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' || *p == ','))
        ++p;
    return p;
}

static int fparse(const char *d, FPath *f) {
    f->nsub = 0;
    f->cnt[0] = 0;
    int total = 0;
    const char *p = fskip(d);
    if (*p != 'M' && *p != 'm') return -1;
    double cx = 0, cy = 0, sx = 0, sy = 0;
    int sub = -1;
    char cmd = 0;
    while (1) {
        p = fskip(p);
        if (!*p) break;
        if (*p == 'M' || *p == 'm' || *p == 'L' || *p == 'l' || *p == 'Z' || *p == 'z') {
            cmd = *p++;
        }
        if (cmd == 'Z' || cmd == 'z') {
            if (sub >= 0) {
                if (f->cnt[sub] < 2) return -3;
                sub = -1;
            }
            cx = sx; cy = sy;
            continue;
        }
        p = fskip(p);
        char *end = 0;
        double nx = strtod(p, &end);
        if (end == p) return -2;
        p = fskip(end);
        double ny = strtod(p, &end);
        if (end == p) return -2;
        p = end;
        if (cmd == 'm' || cmd == 'l') { nx += cx; ny += cy; }
        if (cmd == 'M' || cmd == 'm') {
            if (f->nsub == FMAXS) return -4;
            sub = f->nsub++;
            f->first[sub] = total;
            f->cnt[sub] = 0;
            f->v[total++] = (P){nx, ny};
            f->cnt[sub] = 1;
            sx = nx; sy = ny;
            cmd = 'L';   /* further coordinate pairs are linetos */
        } else {
            if (sub < 0 || total >= 2048) return -4;
            f->v[total++] = (P){nx, ny};
            f->cnt[sub]++;
        }
        cx = nx; cy = ny;
    }
    if (sub >= 0 && f->cnt[sub] < 2) return -3;
    return 0;
}

static double fsub_area(const FPath *f, int s) {
    double a = 0.0;
    int base = f->first[s], n = f->cnt[s];
    for (int i = 0; i < n; ++i) {
        P u = f->v[base + i], w = f->v[base + (i + 1) % n];
        a += u.x * w.y - w.x * u.y;
    }
    return 0.5 * a;
}

static int fderive_rule(const FPath *f) {
    int sum = 0, live = 0;
    for (int s = 0; s < f->nsub; ++s) {
        double a = fsub_area(f, s);
        if (fabs(a) > 1e-9) { ++live; sum += (a > 0) ? 1 : -1; }
    }
    return (live > 0 && abs(sum) == live) ? 1 : 0;   /* 1=nonzero 0=evenodd */
}

typedef struct { double x; int dir; } XC;

static int xc_cmp(const void *a, const void *b) {
    double d = ((const XC*)a)->x - ((const XC*)b)->x;
    return (d < 0) ? -1 : (d > 0);
}

/* Winding-aware scanline coverage: crossings for one row are collected and
 * sorted once, then sampled x positions are classified by walking the sorted
 * intervals. */
static void row_cover(const FPath *f, int rule, double py, double minx,
                      double dx, int x0, int x1, int ss, unsigned char *hits) {
    static XC xs[4096];
    int nx = 0;
    for (int s = 0; s < f->nsub; ++s) {
        int base = f->first[s], n = f->cnt[s];
        for (int i = 0; i < n; ++i) {
            P a = f->v[base + i], b = f->v[base + (i + 1) % n];
            if ((a.y > py) == (b.y > py)) continue;
            if (nx >= 4096) break;
            double t = (py - a.y) / (b.y - a.y);
            xs[nx].x = a.x + (b.x - a.x) * t;
            xs[nx].dir = (b.y > a.y) ? 1 : -1;
            ++nx;
        }
    }
    qsort(xs, (size_t)nx, sizeof(XC), xc_cmp);
    for (int ix = x0; ix <= x1; ++ix) {
        hits[ix] = 0;
        for (int j = 0; j < ss; ++j) {
            double px = minx + (ix + (j + 0.5) / ss) * dx;
            /* winding at px: sum dirs of crossings strictly to the right */
            int wsum = 0;
            for (int k = nx - 1; k >= 0 && xs[k].x > px; --k) wsum += xs[k].dir;
            int on;
            if (rule == 1) on = (wsum != 0);
            else on = ((wsum % 2) != 0);
            if (on) hits[ix] = (unsigned char)(hits[ix] + 1);
        }
    }
}

static int cmd_fill(const char *path, int w, int h, int ss, int rule,
                    const char *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -5;
    static char d[65536];
    size_t n = fread(d, 1, sizeof(d) - 1, f);
    d[n] = 0;
    fclose(f);
    static FPath fp;
    int pr = fparse(d, &fp);
    if (pr != 0) return 2;
    if (rule < 0) rule = fderive_rule(&fp);
    if (ss < 1 || ss > 16) return 2;

    double minx = fp.v[0].x, maxx = minx, miny = fp.v[0].y, maxy = miny;
    for (int s = 0; s < fp.nsub; ++s)
        for (int i = 0; i < fp.cnt[s]; ++i) {
            P q = fp.v[fp.first[s] + i];
            if (q.x < minx) minx = q.x;
            if (q.x > maxx) maxx = q.x;
            if (q.y < miny) miny = q.y;
            if (q.y > maxy) maxy = q.y;
        }
    double dx = (maxx - minx) / w, dy = (maxy - miny) / h;

    unsigned char *buf = (unsigned char*)calloc((size_t)w * h, 1);
    unsigned char *hits = (unsigned char*)calloc((size_t)w, 1);
    if (!buf || !hits) { free(buf); free(hits); return 3; }
    int total = ss * ss;
    for (int y = 0; y < h; ++y) {
        for (int j = 0; j < ss; ++j) {
            double py = miny + (y + (j + 0.5) / ss) * dy;
            row_cover(&fp, rule, py, minx, dx, 0, w - 1, ss, hits);
            for (int x = 0; x < w; ++x) buf[y * w + x] += hits[x];
        }
    }
    for (int i = 0; i < w * h; ++i)
        buf[i] = (buf[i] * 2 >= total) ? 255 : 0;

    if (out && strcmp(out, "-") != 0) {
        FILE *g = fopen(out, "wb");
        if (!g) { free(buf); free(hits); return 4; }
        fprintf(g, "P5\n%d %d\n255\n", w, h);
        fwrite(buf, 1, (size_t)w * h, g);
        fclose(g);
    } else {
        printf("P5\n%d %d\n255\n", w, h);
        fwrite(buf, 1, (size_t)w * h, stdout);
    }
    free(buf); free(hits);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    if (!strcmp(argv[1], "area")) return cmd_area(argv[2]);
    if (!strcmp(argv[1], "pixels") && argc >= 5) return cmd_pixels(argv[2], atoi(argv[3]), atoi(argv[4]));
    if (!strcmp(argv[1], "points") && argc >= 4) return cmd_points(argv[2], atoi(argv[3]));
    if (!strcmp(argv[1], "fill") && argc >= 5) {
        int ss = (argc > 5) ? atoi(argv[5]) : 8;
        int rule = -1;
        if (argc > 6 && strcmp(argv[6], "auto") != 0)
            rule = strcmp(argv[6], "evenodd") == 0 ? 0 : 1;
        const char *out = (argc > 7) ? argv[7] : 0;
        return cmd_fill(argv[2], atoi(argv[3]), atoi(argv[4]), ss, rule, out);
    }
    return 1;
}
