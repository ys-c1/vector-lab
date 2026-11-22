/* 独立 gold 参照：精确整数定向谓词判定线段相交，与主代码的 double 浮点
 * 管线算法完全不同。
 * 输入：每行 "x0 y0 x1 y1"，最多 6 位小数（按 1e6 放大为整数，不经过浮点）。
 * 输出：首行 "n=<段数> truth=<相交对数> zero=<零长段数>"，其后每行一对 "i j"
 *       （i < j），覆盖真交、端点接触、共线重叠、点在线上、点相等。
 * 吸附：可选第三参 snap（坐标单位），近失对在该精确整数距离内也判交，
 *       用于表达「几何意图相交但输入带错位」的真值；默认 0，即严格精确判交。
 * 用法：ref <master.txt> [snap]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAXN 300
#define SCALE 1000000LL

typedef struct { int64_t x, y; } P;
typedef struct { P a, b; } S;

/* 字符串定点转整数：支持 "1"、"1.2"、"-0.000001"，小数补齐/截断到 6 位 */
static int64_t fix(const char *t) {
    int neg = 0;
    if (*t == '-') { neg = 1; ++t; } else if (*t == '+') ++t;
    long long ip = 0, frac = 0;
    int digits = 0;
    while (*t >= '0' && *t <= '9') { ip = ip * 10 + (*t - '0'); ++t; }
    if (*t == '.') {
        ++t;
        while (*t >= '0' && *t <= '9' && digits < 6) {
            frac = frac * 10 + (*t - '0'); ++digits; ++t;
        }
        while (*t >= '0' && *t <= '9') ++t;   /* 超过 6 位的数据不生成，防御 */
    }
    while (digits < 6) { frac *= 10; ++digits; }
    int64_t v = (int64_t)(ip * SCALE + frac);
    return neg ? -v : v;
}

/* snap 参数（坐标单位）转定点整数，四舍五入 */
static int64_t fix_snap(const char *t) {
    double v = atof(t);
    if (v < 0) v = 0;
    return (int64_t)(v * (double)SCALE + (v >= 0 ? 0.5 : -0.5));
}

static int eq(P a, P b) { return a.x == b.x && a.y == b.y; }

static __int128 orient(P a, P b, P c) {
    return (__int128)(b.x - a.x) * (c.y - a.y)
         - (__int128)(b.y - a.y) * (c.x - a.x);
}
static int sgn(__int128 v) { return v < 0 ? -1 : v > 0 ? 1 : 0; }

static int in_box(P a, P b, P p) {
    int64_t lx = a.x < b.x ? a.x : b.x, hx = a.x > b.x ? a.x : b.x;
    int64_t ly = a.y < b.y ? a.y : b.y, hy = a.y > b.y ? a.y : b.y;
    return p.x >= lx && p.x <= hx && p.y >= ly && p.y <= hy;
}
static int on_seg(S s, P p) {
    return orient(s.a, s.b, p) == 0 && in_box(s.a, s.b, p);
}

/* 共线区间重叠（all-orient-zero 之后调用） */
static int overlap_1d(int64_t a0, int64_t a1, int64_t b0, int64_t b1) {
    if (a0 > a1) { int64_t t = a0; a0 = a1; a1 = t; }
    if (b0 > b1) { int64_t t = b0; b0 = b1; b1 = t; }
    return a1 >= b0 && b1 >= a0;
}

static int intersect(S u, S v) {
    int uz = eq(u.a, u.b), vz = eq(v.a, v.b);
    if (uz && vz) return eq(u.a, v.a);
    if (uz) return on_seg(v, u.a);
    if (vz) return on_seg(u, v.a);
    int o1 = sgn(orient(u.a, u.b, v.a));
    int o2 = sgn(orient(u.a, u.b, v.b));
    int o3 = sgn(orient(v.a, v.b, u.a));
    int o4 = sgn(orient(v.a, v.b, u.b));
    if (((o1 > 0 && o2 < 0) || (o1 < 0 && o2 > 0)) &&
        ((o3 > 0 && o4 < 0) || (o3 < 0 && o4 > 0)))
        return 1;
    if (o1 == 0 && on_seg(u, v.a)) return 1;
    if (o2 == 0 && on_seg(u, v.b)) return 1;
    if (o3 == 0 && on_seg(v, u.a)) return 1;
    if (o4 == 0 && on_seg(v, u.b)) return 1;
    if (o1 == 0 && o2 == 0 && o3 == 0 && o4 == 0) {
        /* 共线：x 区间不退化就比 x，否则比 y */
        if (u.a.x != u.b.x || v.a.x != v.b.x)
            return overlap_1d(u.a.x, u.b.x, v.a.x, v.b.x);
        return overlap_1d(u.a.y, u.b.y, v.a.y, v.b.y);
    }
    return 0;
}

/* snap 判定：严格相交，或段-段最小距离 <= snap。四端点对另一线段的垂距取 min
 * （二维不相交线段最近点必在边界）。投影在段内时分数距离 cross^2/l2 与 snap^2
 * 做交叉相乘精确比较，全程 __int128，无开方无除法。 */
static int within_snap(S u, S v, int64_t snap) {
    if (intersect(u, v)) return 1;
    if (snap <= 0) return 0;
    __int128 s2 = (__int128)snap * snap;
    P pts[4] = {u.a, u.b, v.a, v.b};
    S segs[4] = {v, v, u, u};
    for (int pi = 0; pi < 4; ++pi) {
        S seg = segs[pi];
        P p = pts[pi];
        if (eq(seg.a, seg.b)) {
            __int128 dx = p.x - seg.a.x, dy = p.y - seg.a.y;
            if (dx * dx + dy * dy <= s2) return 1;
            continue;
        }
        __int128 vx = seg.b.x - seg.a.x, vy = seg.b.y - seg.a.y;
        __int128 l2 = vx * vx + vy * vy;
        __int128 wx = p.x - seg.a.x, wy = p.y - seg.a.y;
        __int128 tt = wx * vx + wy * vy;
        if (tt <= 0) {
            __int128 dx = p.x - seg.a.x, dy = p.y - seg.a.y;
            if (dx * dx + dy * dy <= s2) return 1;
        } else if (tt >= l2) {
            __int128 dx = p.x - seg.b.x, dy = p.y - seg.b.y;
            if (dx * dx + dy * dy <= s2) return 1;
        } else {
            __int128 cr = vx * wy - vy * wx;
            if (cr * cr <= s2 * l2) return 1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s <master.txt> [snap]\n", argv[0]); return 1; }
    int64_t snap = argc >= 3 ? fix_snap(argv[2]) : 0;
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("fopen"); return 2; }
    S seg[MAXN];
    int n = 0, zero = 0;
    char t0[64], t1[64], t2[64], t3[64];
    while (n < MAXN && fscanf(f, "%63s %63s %63s %63s", t0, t1, t2, t3) == 4) {
        seg[n].a.x = fix(t0); seg[n].a.y = fix(t1);
        seg[n].b.x = fix(t2); seg[n].b.y = fix(t3);
        if (eq(seg[n].a, seg[n].b)) ++zero;
        ++n;
    }
    fclose(f);
    int pairs_i[MAXN * MAXN], pairs_j[MAXN * MAXN], k = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
            if (within_snap(seg[i], seg[j], snap)) {
                pairs_i[k] = i; pairs_j[k] = j; ++k;
            }
    printf("n=%d truth=%d zero=%d snap=%lld\n",
           n, k, zero, (long long)snap);
    for (int q = 0; q < k; ++q) printf("%d %d\n", pairs_i[q], pairs_j[q]);
    return 0;
}
