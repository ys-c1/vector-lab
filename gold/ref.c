/* 独立参照实现：与主代码不同算法，只负责产出可比对的文件。
 * 用法：
 *   ref area  <poly.txt>          输出面积，6 位小数
 *   ref pixels <poly.txt> <w> <h> 输出 PGM，8x8 超采样
 *   ref points <line.txt> <n>     输出 n 等分点（按累计弧长）
 *   ref layers <points.txt> <target>   凸层剥解（gift wrapping），
 *                                       输出到首个达标层的层文件
 *   ref lpgm <points.txt> <target> <w> <h>  凸层灰度 PGM（参照层图）
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

/* ---- 凸层剥解（onion peeling）独立参照 ----
 * 与主模块的 monotone-chain 不同，这里用 gift wrapping（Jarvis march）
 * 逐圈取凸包，再以边点检测剥去边界点，直到不足 3 个非共线点。 */

#define MAXL 64
typedef struct { int count; P pts[MAXP]; } GPoly;
typedef struct { int nl; GPoly lay[MAXL]; int nrem; P rem[MAXP]; } GLayers;

static double gcross(P o, P a, P b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

static int g_on_edge(P a, P b, P q) {
    const double EPS = 1e-10;
    double cr = (b.x - a.x) * (q.y - a.y) - (b.y - a.y) * (q.x - a.x);
    if (fabs(cr) > EPS * (1.0 + fabs(b.x - a.x) + fabs(b.y - a.y)))
        return 0;
    if (q.x < fmin(a.x, b.x) - EPS || q.x > fmax(a.x, b.x) + EPS) return 0;
    if (q.y < fmin(a.y, b.y) - EPS || q.y > fmax(a.y, b.y) + EPS) return 0;
    return 1;
}

/* gift wrapping：返回凸包顶点数（逆时针），共线/不足 3 点返回 0。
 * 注意只排除当前点；每轮在全部点里找最右转向的候选。 */
static int g_wrap(const P *s, int m, GPoly *h) {
    int first = 0;
    for (int i = 1; i < m; ++i)
        if (s[i].x < s[first].x ||
            (s[i].x == s[first].x && s[i].y < s[first].y))
            first = i;
    int cur = first, cnt = 0;
    do {
        if (cnt >= MAXP) return 0;
        h->pts[cnt++] = s[cur];
        int nxt = -1;
        for (int j = 0; j < m; ++j) {
            if (j == cur) continue;
            if (nxt < 0) { nxt = j; continue; }
            double c = gcross(s[cur], s[nxt], s[j]);
            if (c < 0.0 || (c == 0.0 &&
                (s[j].x - s[cur].x) * (s[j].x - s[cur].x) +
                (s[j].y - s[cur].y) * (s[j].y - s[cur].y) >
                (s[nxt].x - s[cur].x) * (s[nxt].x - s[cur].x) +
                (s[nxt].y - s[cur].y) * (s[nxt].y - s[cur].y)))
                nxt = j;
        }
        if (nxt < 0) break;
        cur = nxt;
    } while (cur != first);
    if (cnt < 3) return 0;
    /* 退化检查：全部点共线时面积为 0。 */
    double area2 = 0.0;
    for (int i = 0; i < cnt; ++i) {
        P u = h->pts[i], v = h->pts[(i + 1) % cnt];
        area2 += u.x * v.y - v.x * u.y;
    }
    if (fabs(area2) <= 1e-12) return 0;
    h->count = cnt;
    return cnt;
}

static int g_peel(const P *in, int n, GLayers *ls) {
    P work[MAXP];
    int m = n;
    for (int i = 0; i < n; ++i) work[i] = in[i];
    ls->nl = 0;
    while (m >= 3) {
        if (ls->nl >= MAXL) return -2;
        GPoly h;
        if (g_wrap(work, m, &h) == 0) break;
        ls->lay[ls->nl++] = h;
        P keep[MAXP];
        int nm = 0;
        for (int i = 0; i < m; ++i) {
            int boundary = 0;
            for (int e = 0; e < h.count; ++e)
                if (g_on_edge(h.pts[e], h.pts[(e + 1) % h.count], work[i]))
                    { boundary = 1; break; }
            if (!boundary) keep[nm++] = work[i];
        }
        for (int i = 0; i < nm; ++i) work[i] = keep[i];
        m = nm;
        if (m < 3) break;
    }
    ls->nrem = m;
    for (int i = 0; i < m; ++i) ls->rem[i] = work[i];
    return 0;
}

static double g_area(const GPoly *p) {
    if (p->count < 3) return 0.0;
    double a = 0.0;
    for (int i = 0; i < p->count; ++i) {
        P u = p->pts[i], v = p->pts[(i + 1) % p->count];
        a += u.x * v.y - v.x * u.y;
    }
    return fabs(a) * 0.5;
}

static int g_poly_inside(const GPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->count; ++i) {
        P a = p->pts[i], b = p->pts[(i + 1) % p->count];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

/* 与主模块 hull_box 同参数、同规则的视窗（margin 固定 0.02）。 */
static void g_box(const GPoly *p, double margin,
                  double *minx, double *miny, double *maxx, double *maxy) {
    *minx = *maxx = p->pts[0].x;
    *miny = *maxy = p->pts[0].y;
    for (int i = 1; i < p->count; ++i) {
        if (p->pts[i].x < *minx) *minx = p->pts[i].x;
        if (p->pts[i].x > *maxx) *maxx = p->pts[i].x;
        if (p->pts[i].y < *miny) *miny = p->pts[i].y;
        if (p->pts[i].y > *maxy) *maxy = p->pts[i].y;
    }
    double dx = (*maxx - *minx) * margin, dy = (*maxy - *miny) * margin;
    if (dx < 1e-9) dx = (fabs(*maxx) + 1.0) * margin;
    if (dy < 1e-9) dy = (fabs(*maxy) + 1.0) * margin;
    *minx -= dx; *maxx += dx; *miny -= dy; *maxy += dy;
}

static unsigned char g_gray(int layer) {
    int g = 255 - 16 * layer;
    if (g < 16) g = 16;
    return (unsigned char)g;
}

/* 输出 0..klayer 的层文件（文本），供逐行比对。 */
static int g_emit_layers(const GLayers *ls, int klayer) {
    for (int i = 0; i <= klayer; ++i) {
        printf("L%d %d\n", i, ls->lay[i].count);
        for (int j = 0; j < ls->lay[i].count; ++j)
            printf("%.6f %.6f\n", ls->lay[i].pts[j].x, ls->lay[i].pts[j].y);
    }
    return 0;
}

static int cmd_layers(const char *path, double target) {
    P in[MAXP];
    int n = load(path, in);
    if (n < 1 || target <= 0.0 || target >= 1.0) return 2;
    GLayers ls;
    int rc = g_peel(in, n, &ls);
    if (rc == -2) { fprintf(stderr, "REJECT layer cap\n"); return 4; }
    fprintf(stderr, "points=%d total_layers=%d remaining=%d\n",
            n, ls.nl, ls.nrem);
    if (ls.nl == 0) {
        fprintf(stderr, "REJECT degenerate set, %d points remain\n", ls.nrem);
        return 5;
    }
    double base = g_area(&ls.lay[0]);
    int k = -1;
    for (int i = 0; i < ls.nl; ++i)
        if (g_area(&ls.lay[i]) / base < target) { k = i; break; }
    if (k < 0) {
        fprintf(stderr,
                "REJECT target %.4f not reached after %d layers "
                "(cap = total layers = %d)\n",
                target, ls.nl, ls.nl);
        return 6;
    }
    double ratio = g_area(&ls.lay[k]) / base;
    fprintf(stderr, "solved_layer=%d remaining_area_ratio=%.6f\n",
            k + 1, ratio);
    g_emit_layers(&ls, k);
    return 0;
}

static int cmd_lpgm(const char *path, double target, int w, int h) {
    P in[MAXP];
    int n = load(path, in);
    if (n < 1 || w <= 0 || h <= 0 || target <= 0.0 || target >= 1.0) return 2;
    GLayers ls;
    int rc = g_peel(in, n, &ls);
    if (rc == -2) { fprintf(stderr, "REJECT layer count exceeds cap=%d\n", MAXL); return 4; }
    if (rc != 0 || ls.nl == 0) return rc == 0 ? 5 : rc;
    double base = g_area(&ls.lay[0]);
    int k = -1;
    for (int i = 0; i < ls.nl; ++i)
        if (g_area(&ls.lay[i]) / base < target) { k = i; break; }
    if (k < 0) return 6;

    double minx, miny, maxx, maxy;
    g_box(&ls.lay[0], 0.02, &minx, &miny, &maxx, &maxy);
    double dx = (maxx - minx) / w, dy = (maxy - miny) / h;
    unsigned char *buf = (unsigned char*)calloc((size_t)w * h, 1);
    if (!buf) return 3;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double px = minx + (x + 0.5) * dx;
            double py = miny + (y + 0.5) * dy;
            for (int i = k; i >= 0; --i)
                if (g_poly_inside(&ls.lay[i], px, py)) {
                    buf[y * w + x] = g_gray(i);
                    break;
                }
        }
    printf("P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * h, stdout);
    free(buf);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) return 1;
    if (!strcmp(argv[1], "area")) return cmd_area(argv[2]);
    if (!strcmp(argv[1], "pixels") && argc >= 5) return cmd_pixels(argv[2], atoi(argv[3]), atoi(argv[4]));
    if (!strcmp(argv[1], "points") && argc >= 4) return cmd_points(argv[2], atoi(argv[3]));
    if (!strcmp(argv[1], "layers") && argc >= 4) return cmd_layers(argv[2], atof(argv[3]));
    if (!strcmp(argv[1], "lpgm") && argc >= 6) return cmd_lpgm(argv[2], atof(argv[3]), atoi(argv[4]), atoi(argv[5]));
    return 1;
}
