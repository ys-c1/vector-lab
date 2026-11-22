/* 独立参照实现：与主代码不同算法，只负责产出可比对的文件。
 * 用法：
 *   ref area  <poly.txt>          输出面积，6 位小数
 *   ref pixels <poly.txt> <w> <h> 输出 PGM，8x8 超采样
 *   ref points <line.txt> <n>     输出 n 等分点（按累计弧长）
 *   ref layers <scene.txt> <w> <h>  图层依赖场景，输出 PGM（P5）
 * 输入格式：每行 "x y"，多边形首尾自动闭合。
 */
#define _POSIX_C_SOURCE 200809L
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

/* ---------- 图层依赖场景：独立实现（与 render/ 的 Kahn + 表驱动 DP 不同） ---------- */

#define NL 32
#define NM 16
#define NPT 64
typedef struct {
    char name[NM];
    int shade;
    int npts;
    P pts[NPT];
} GLayer;

static int g_find(const GLayer *ls, int n, const char *name) {
    for (int i = 0; i < n; ++i)
        if (!strncmp(ls[i].name, name, NM)) return i;
    return -1;
}

/* DFS 环检测：返回环上节点（含重复起点），无环返回 0 */
static int g_find_cycle(const int *succ, int n, int *cyc, int *cyc_n) {
    enum { WHITE, GRAY, BLACK };
    int color[NL] = {WHITE};
    int stk[NL];
    for (int s = 0; s < n; ++s) {
        if (color[s] != WHITE) continue;
        /* 显式栈的迭代 DFS，避免与主代码一样的递归写法 */
        int node[NL], edge[NL], sp = 0;
        node[0] = s; edge[0] = 0; sp = 1;
        color[s] = GRAY; stk[0] = s;
        while (sp > 0) {
            int u = node[sp - 1];
            int *e = &edge[sp - 1];
            int found = 0;
            while (*e < n) {
                int v = (*e)++;
                if (!succ[u * NL + v]) continue;
                if (color[v] == GRAY) {
                    int k = 0;
                    while (stk[k] != v) ++k;
                    int m = 0;
                    for (int i = k; i < sp; ++i) cyc[m++] = stk[i];
                    cyc[m++] = v;
                    *cyc_n = m;
                    return 1;
                }
                if (color[v] == WHITE) {
                    color[v] = GRAY;
                    stk[sp] = v;
                    node[sp] = v; edge[sp] = 0;
                    ++sp;
                    found = 1;
                    break;
                }
            }
            if (!found) { color[u] = BLACK; --sp; }
        }
    }
    *cyc_n = 0;
    return 0;
}

/* 独立的最长层级计算：迭代松弛（Bellman-Ford 风格多趟扫描），
 * 与主代码的拓扑表驱动单趟松弛是不同实现。
 * ee(v) = 1 + max ee(u)，u 为 v 的前驱；源点为 1。 */
static void g_earliest(const int *succ, int n, int *ee) {
    for (int i = 0; i < n; ++i) ee[i] = 1;
    for (int pass = 0; pass < n; ++pass) {
        int changed = 0;
        for (int u = 0; u < n; ++u)
            for (int v = 0; v < n; ++v)
                if (succ[u * NL + v] && ee[u] + 1 > ee[v]) {
                    ee[v] = ee[u] + 1;
                    changed = 1;
                }
        if (!changed) break;
    }
}

/* 渲染顺序：位掩码就绪集 + 每趟 (ee,id) 最小选择，
 * 与主代码的 Kahn 计数数组实现手法不同，但同一排序规则保证结果一致。 */
static int g_render_order(const int *succ, int n, const int *ee, int *order) {
    unsigned long long taken = 0ULL;
    for (int step = 0; step < n; ++step) {
        int pick = -1;
        for (int v = 0; v < n; ++v) {
            if (taken & (1ULL << v)) continue;
            int ready = 1;
            for (int u = 0; u < n; ++u)
                if (succ[u * NL + v] && !(taken & (1ULL << u))) { ready = 0; break; }
            if (!ready) continue;
            if (pick < 0 || ee[v] < ee[pick] ||
                (ee[v] == ee[pick] && v < pick))
                pick = v;
        }
        if (pick < 0) return -1;
        order[step] = pick;
        taken |= (1ULL << pick);
    }
    return 0;
}

static int inside_g(const GLayer *L, double x, double y) {
    int c = 0;
    for (int i = 0; i < L->npts; ++i) {
        P a = L->pts[i], b = L->pts[(i + 1) % L->npts];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

static int cmd_layers(const char *path, int w, int h) {
    if (w <= 0 || h <= 0) return 2;
    FILE *f = fopen(path, "r");
    if (!f) return 2;

    GLayer ls[NL];
    int nl = 0;
    int succ[NL * NL];
    memset(succ, 0, sizeof(succ));
    char ea[NL][NM], eb[NL][NM];
    int ne = 0;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    int have_box = 0;

    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *nl2 = strchr(line, '\n');
        if (nl2) *nl2 = '\0';
        char *sv = NULL;
        char *kw = strtok_r(line, " \t\r", &sv);
        if (!kw || kw[0] == '#') continue;

        if (!strcmp(kw, "win")) {
            double v[4]; int k = 0;
            for (; k < 4; ++k) {
                char *t = strtok_r(NULL, " \t\r", &sv);
                if (!t) break;
                v[k] = atof(t);
            }
            if (k == 4) { minx = v[0]; miny = v[1]; maxx = v[2]; maxy = v[3]; have_box = 1; }
        } else if (!strcmp(kw, "layer")) {
            if (nl >= NL) { fclose(f); return 2; }
            char *name = strtok_r(NULL, " \t\r", &sv);
            char *sh = strtok_r(NULL, " \t\r", &sv);
            if (!name || !sh) { fclose(f); return 2; }
            GLayer *L = &ls[nl];
            memset(L, 0, sizeof(*L));
            snprintf(L->name, NM, "%s", name);
            L->shade = atoi(sh);
            int npts = 0;
            char *tx, *ty;
            while ((tx = strtok_r(NULL, " \t\r", &sv)) &&
                   (ty = strtok_r(NULL, " \t\r", &sv))) {
                if (npts >= NPT) { fclose(f); return 2; }
                L->pts[npts].x = atof(tx);
                L->pts[npts].y = atof(ty);
                ++npts;
            }
            if (npts < 3) { fclose(f); return 2; }
            L->npts = npts;
            if (!have_box) {
                for (int i = 0; i < npts; ++i) {
                    double x = L->pts[i].x, y = L->pts[i].y;
                    if (nl == 0 && i == 0) { minx = maxx = x; miny = maxy = y; }
                    if (x < minx) minx = x;
                    if (x > maxx) maxx = x;
                    if (y < miny) miny = y;
                    if (y > maxy) maxy = y;
                }
            }
            ++nl;
        } else if (!strcmp(kw, "edge")) {
            char *a = strtok_r(NULL, " \t\r", &sv);
            char *b = strtok_r(NULL, " \t\r", &sv);
            if (!a || !b || ne >= NL) { fclose(f); return 2; }
            snprintf(ea[ne], NM, "%s", a);
            snprintf(eb[ne], NM, "%s", b);
            ++ne;
        }
        /* order 记录被参照实现有意忽略：参照只按依赖图自身定序 */
    }
    fclose(f);
    if (nl == 0) return 2;
    for (int i = 0; i < ne; ++i) {
        int u = g_find(ls, nl, ea[i]), v = g_find(ls, nl, eb[i]);
        if (u < 0 || v < 0) return 2;
        succ[u * NL + v] = 1;
    }

    int cyc[NL + 1], cyc_n;
    if (g_find_cycle(succ, nl, cyc, &cyc_n)) {
        fprintf(stderr, "gold: cycle detected\n");
        return 4;
    }

    int ee[NL];
    g_earliest(succ, nl, ee);

    int order[NL];
    if (g_render_order(succ, nl, ee, order) != 0) {
        fprintf(stderr, "gold: topo failed\n");
        return 4;
    }

    double dx = (maxx - minx) / w, dy = (maxy - miny) / h;
    unsigned char *buf = (unsigned char*)calloc((size_t)w * h, 1);
    if (!buf) return 3;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double px = minx + (x + 0.5) * dx;
            double py = miny + (y + 0.5) * dy;
            int val = 0;
            for (int k = 0; k < nl; ++k)
                if (inside_g(&ls[order[k]], px, py))
                    val = ls[order[k]].shade;
            buf[y * w + x] = (unsigned char)val;
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
    if (!strcmp(argv[1], "layers") && argc >= 5) return cmd_layers(argv[2], atoi(argv[3]), atoi(argv[4]));
    return 1;
}
