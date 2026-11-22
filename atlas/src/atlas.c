#define _POSIX_C_SOURCE 199309L
#include "atlas.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_ORDERS 5
#define MAXFREE (ATLAS_MAX_TILES + 2)

typedef struct { int x, y, w, h; } FRect;

/* ---------------- 排序 ---------------- */

typedef struct { double k1, k2; int idx; } SortKey;

static int cmp_key(const void *a, const void *b) {
    const SortKey *p = (const SortKey *)a, *q = (const SortKey *)b;
    if (p->k1 > q->k1) return -1;
    if (p->k1 < q->k1) return 1;
    if (p->k2 > q->k2) return -1;
    if (p->k2 < q->k2) return 1;
    return p->idx - q->idx;
}

/* 生成若干确定性排序，供装箱组合尝试。 */
static int build_orders(const Tiles *t, int ord[MAX_ORDERS][ATLAS_MAX_TILES]) {
    int n = t->n;
    SortKey key[ATLAS_MAX_TILES];

    for (int mode = 0; mode < MAX_ORDERS; ++mode) {
        for (int i = 0; i < n; ++i) {
            int w = t->t[i].w, h = t->t[i].h;
            double a = (double)w * h;
            key[i].idx = i;
            switch (mode) {
            case 0: key[i].k1 = a; key[i].k2 = (double)(w > h ? w : h); break;
            case 1: key[i].k1 = (double)(w > h ? w : h); key[i].k2 = a; break;
            case 2: key[i].k1 = (double)h; key[i].k2 = (double)w; break;
            case 3: key[i].k1 = (double)w; key[i].k2 = (double)h; break;
            default: key[i].k1 = (double)w + h; key[i].k2 = a; break;
            }
        }
        qsort(key, (size_t)n, sizeof(SortKey), cmp_key);
        for (int i = 0; i < n; ++i) ord[mode][i] = key[i].idx;
    }
    return MAX_ORDERS;
}

/* ---------------- 货架式装箱 ---------------- */

typedef struct { int y, h, usedw; } Row;

static int shelf_pack(const Tiles *t, const int *ord, int n, int side, FRect *out) {
    Row rows[ATLAS_MAX_TILES];
    int nr = 0, next_y = 0;
    for (int i = 0; i < n; ++i) {
        int idx = ord[i], w = t->t[idx].w, h = t->t[idx].h;
        if (w > side || h > side) return 0;
        int best = -1, best_left = 0;
        for (int r = 0; r < nr; ++r) {
            if (h > rows[r].h || rows[r].usedw + w > side) continue;
            int left = rows[r].h - h;
            if (best < 0 || left < best_left) { best = r; best_left = left; }
        }
        if (best >= 0) {
            out[i].x = rows[best].usedw;
            out[i].y = rows[best].y;
            out[i].w = w;
            out[i].h = h;
            rows[best].usedw += w;
        } else {
            if (next_y + h > side) return 0;
            rows[nr].y = next_y;
            rows[nr].h = h;
            rows[nr].usedw = w;
            out[i].x = 0;
            out[i].y = next_y;
            out[i].w = w;
            out[i].h = h;
            next_y += h;
            nr++;
        }
    }
    return 1;
}

/* ---------------- 断头台切割装箱 ---------------- */

static int guillotine_pack(const Tiles *t, const int *ord, int n, int side, int mode, FRect *out) {
    FRect fr[MAXFREE];
    int nf = 1;
    fr[0] = (FRect){0, 0, side, side};
    for (int i = 0; i < n; ++i) {
        int idx = ord[i], w = t->t[idx].w, h = t->t[idx].h;
        if (w > side || h > side) return 0;
        int best = -1;
        long long best_score = 0;
        for (int k = 0; k < nf; ++k) {
            if (fr[k].w < w || fr[k].h < h) continue;
            long long lw = fr[k].w - w, lh = fr[k].h - h;
            long long score;
            if (mode == 0) score = lw * fr[k].h + lh * w - lw * lh;      /* 剩余面积 */
            else if (mode == 1) score = (lw < lh ? lw : lh);             /* 短边 */
            else score = (lw > lh ? lw : lh);                            /* 长边 */
            if (best < 0 || score < best_score) { best = k; best_score = score; }
        }
        if (best < 0) return 0;
        FRect f = fr[best];
        out[i] = (FRect){f.x, f.y, w, h};
        fr[best] = fr[--nf];

        long long rw = f.w - w, rh = f.h - h;
        int horiz = (mode == 2) ? (rw <= rh) : (rw > rh);
        FRect a, b;
        if (horiz) {
            a = (FRect){f.x, f.y + h, f.w, (int)rh};     /* 顶部整宽 */
            b = (FRect){f.x + w, f.y, (int)rw, h};        /* 右侧贴条 */
        } else {
            a = (FRect){f.x + w, f.y, (int)rw, f.h};      /* 右侧整高 */
            b = (FRect){f.x, f.y + h, w, (int)rh};        /* 顶部贴条 */
        }
        if (a.w > 0 && a.h > 0 && nf < MAXFREE) fr[nf++] = a;
        if (b.w > 0 && b.h > 0 && nf < MAXFREE) fr[nf++] = b;
    }
    return 1;
}

/* ---------------- 天际线（左下优先）装箱 ---------------- */

typedef struct { int x, y, w; } SkyNode;

static int skyline_pack(const Tiles *t, const int *ord, int n, int side, FRect *out) {
    SkyNode sky[ATLAS_MAX_TILES + 2];
    int ns = 1;
    sky[0] = (SkyNode){0, 0, side};
    for (int i = 0; i < n; ++i) {
        int idx = ord[i], w = t->t[idx].w, h = t->t[idx].h;
        if (w > side || h > side) return 0;
        int best_x = -1, best_y = 0;
        /* 候选 x：0 与每个节点起点；也考虑让图块右缘贴齐节点起点。 */
        for (int c = -1; c < ns; ++c) {
            int x = (c < 0) ? 0 : sky[c].x;
            if (x + w > side) continue;
            int y = 0;
            for (int k = 0; k < ns; ++k) {
                int nx = sky[k].x, nw = sky[k].w;
                if (nx + nw <= x || nx >= x + w) continue;
                if (sky[k].y > y) y = sky[k].y;
            }
            if (best_x < 0 || y < best_y || (y == best_y && x < best_x)) {
                best_x = x;
                best_y = y;
            }
        }
        if (best_x < 0 || best_y + h > side) return 0;
        out[i] = (FRect){best_x, best_y, w, h};
        /* 更新天际线：新段 (best_x, best_y+h, w)，切掉被覆盖部分。 */
        SkyNode next[ATLAS_MAX_TILES + 2];
        int nn = 0;
        int nx0 = best_x, nx1 = best_x + w;
        for (int k = 0; k < ns; ++k) {
            int sx = sky[k].x, sw = sky[k].w, sy = sky[k].y;
            int sx1 = sx + sw;
            if (sx1 <= nx0 || sx >= nx1) { next[nn++] = sky[k]; continue; }
            if (sx < nx0) next[nn++] = (SkyNode){sx, sy, nx0 - sx};
            if (sx1 > nx1) next[nn++] = (SkyNode){nx1, sy, sx1 - nx1};
        }
        next[nn++] = (SkyNode){best_x, best_y + h, w};
        /* 按 x 排序（插入排序，段数很少）。 */
        for (int a = 1; a < nn; ++a) {
            SkyNode v = next[a];
            int b = a - 1;
            while (b >= 0 && next[b].x > v.x) { next[b + 1] = next[b]; --b; }
            next[b + 1] = v;
        }
        /* 合并相邻同高段。 */
        int m = 0;
        for (int k = 0; k < nn; ++k) {
            if (m > 0 && next[k].y == sky[m - 1].y && sky[m - 1].x + sky[m - 1].w == next[k].x)
                sky[m - 1].w += next[k].w;
            else
                sky[m++] = next[k];
        }
        ns = m;
    }
    return 1;
}

/* ---------------- 组合装箱 ---------------- */

static int ords_cache[MAX_ORDERS][ATLAS_MAX_TILES];
static int nords_cache = 0;
static unsigned long long ords_sig = 0;
static int ords_valid = 0;

static unsigned long long tiles_sig(const Tiles *t) {
    unsigned long long h = 1469598103934665603ULL;
    h ^= (unsigned long long)t->n; h *= 1099511628211ULL;
    for (int i = 0; i < t->n; ++i) {
        h ^= (unsigned long long)(unsigned)t->t[i].w; h *= 1099511628211ULL;
        h ^= (unsigned long long)(unsigned)t->t[i].h; h *= 1099511628211ULL;
    }
    return h;
}

static void ensure_orders(const Tiles *t) {
    unsigned long long sig = tiles_sig(t);
    if (ords_valid && sig == ords_sig) return;
    nords_cache = build_orders(t, ords_cache);
    ords_sig = sig;
    ords_valid = 1;
}

/* 尝试全部排序 × 策略，成功则把最优（使用高度最小）的放置写入 out。
 * out 按**图块下标**索引（out[i] 对应 t->t[i]），不是装箱顺序。
 * 返回 1 成功，0 失败。 */
static int pack_best(const Tiles *t, int side, FRect *out) {
    static FRect tmp[ATLAS_MAX_TILES];
    static FRect best[ATLAS_MAX_TILES];
    ensure_orders(t);
    int n = t->n;
    int found = 0;
    int best_h = 0;
    int best_ord = 0;

    for (int o = 0; o < nords_cache; ++o) {
        for (int s = 0; s < 5; ++s) {
            int ok = 0;
            if (s == 0) ok = shelf_pack(t, ords_cache[o], n, side, tmp);
            else if (s == 1) ok = guillotine_pack(t, ords_cache[o], n, side, 0, tmp);
            else if (s == 2) ok = guillotine_pack(t, ords_cache[o], n, side, 1, tmp);
            else if (s == 3) ok = guillotine_pack(t, ords_cache[o], n, side, 2, tmp);
            else ok = skyline_pack(t, ords_cache[o], n, side, tmp);
            if (!ok) continue;
            int used = 0;
            for (int i = 0; i < n; ++i) {
                int b = tmp[i].y + tmp[i].h;
                if (b > used) used = b;
            }
            if (!found || used < best_h) {
                found = 1;
                best_h = used;
                best_ord = o;
                memcpy(best, tmp, (size_t)n * sizeof(FRect));
            }
        }
    }
    if (found && out) {
        /* 把按装箱顺序的放置映射回图块下标。 */
        for (int i = 0; i < n; ++i) out[ords_cache[best_ord][i]] = best[i];
    }
    return found;
}

static int feasible(const Tiles *t, int side) {
    return pack_best(t, side, NULL);
}

/* ---------------- 工具 ---------------- */

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

static long long tiles_area(const Tiles *t) {
    long long a = 0;
    for (int i = 0; i < t->n; ++i) a += (long long)t->t[i].w * t->t[i].h;
    return a;
}

static int max_dim(const Tiles *t) {
    int m = 0;
    for (int i = 0; i < t->n; ++i) {
        if (t->t[i].w > m) m = t->t[i].w;
        if (t->t[i].h > m) m = t->t[i].h;
    }
    return m;
}

/* ceil(sqrt(a))，整数运算避免浮点误差。 */
static long long ceil_sqrt_ll(long long a) {
    if (a <= 0) return 0;
    long long r = (long long)sqrt((double)a);
    while (r * r < a) ++r;
    while (r > 0 && (r - 1) * (r - 1) >= a) --r;
    return r;
}

static int next_pow2(int x) {
    int p = 1;
    while (p < x) p <<= 1;
    return p;
}

int atlas_side_bound(const Tiles *t) {
    if (!t || t->n <= 0) return -1;
    long long area = tiles_area(t);
    int md = max_dim(t);
    long long lower = ceil_sqrt_ll(area);
    if (lower < md) lower = md;
    /* 竖直堆叠一定可行：边长 max(最大宽, 高度之和)；水平同理取较小者。 */
    long long sum_w = 0, sum_h = 0;
    int max_w = 0, max_h = 0;
    for (int i = 0; i < t->n; ++i) {
        sum_w += t->t[i].w;
        sum_h += t->t[i].h;
        if (t->t[i].w > max_w) max_w = t->t[i].w;
        if (t->t[i].h > max_h) max_h = t->t[i].h;
    }
    long long vert = sum_h > max_w ? sum_h : max_w;
    long long horz = sum_w > max_h ? sum_w : max_h;
    long long hi = vert < horz ? vert : horz;
    if (hi < lower) hi = lower;
    if (hi > ATLAS_MAX_SIDE) hi = ATLAS_MAX_SIDE;
    return (int)hi;
}

int atlas_pack(const Tiles *t, int side, int *boxes, int max_boxes) {
    if (!t || t->n <= 0 || side <= 0) return ATLAS_EINPUT;
    int md = max_dim(t);
    if (md > side) return ATLAS_ETOOBIG;
    static FRect out[ATLAS_MAX_TILES];
    if (!pack_best(t, side, out)) return 0;
    if (boxes) {
        int k = t->n < max_boxes ? t->n : max_boxes;
        for (int i = 0; i < k; ++i) {
            boxes[4 * i + 0] = out[i].x;
            boxes[4 * i + 1] = out[i].y;
            boxes[4 * i + 2] = out[i].w;
            boxes[4 * i + 3] = out[i].h;
        }
    }
    return 1;
}

int atlas_solve(const Tiles *t, double max_waste, AtlasResult *out) {
    if (!t || !out) return ATLAS_EINPUT;
    memset(out, 0, sizeof(*out));
    out->limit = max_waste;
    if (t->n <= 0) return ATLAS_EINPUT;          /* 空图集拒绝 */
    if (t->n > ATLAS_MAX_TILES) return ATLAS_EINPUT;
    if (max_waste < 0.0 || max_waste > 1.0) return ATLAS_EINPUT;
    for (int i = 0; i < t->n; ++i)
        if (t->t[i].w <= 0 || t->t[i].h <= 0) return ATLAS_EINPUT;

    out->tiles = t->n;
    out->tile_area = tiles_area(t);

    long long area = out->tile_area;
    int md = max_dim(t);
    if (md > ATLAS_MAX_SIDE) return ATLAS_ETOOBIG;

    double t0 = now_ms();

    /* 下界：面积与最大块边长。上界：堆叠构造保证可行。 */
    long long lb = ceil_sqrt_ll(area);
    if (lb < md) lb = md;
    int lo = (int)lb;
    int hi = atlas_side_bound(t);
    if (hi < lo) hi = (int)lb;

    /* 上界处必须可行；否则倍增直到可行（容错）。 */
    while (!feasible(t, hi)) {
        if (hi >= ATLAS_MAX_SIDE) return ATLAS_EINFEASIBLE;
        hi = hi > ATLAS_MAX_SIDE / 2 ? ATLAS_MAX_SIDE : hi * 2;
    }

    /* 二分最小编装边长：可行则收紧上界，失败则放大下界。 */
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (feasible(t, mid)) hi = mid;
        else lo = mid + 1;
    }
    /* 装箱启发式理论上可能非单调；向下逐格复核，确保 lo-1 真的不可行。 */
    while (lo > lb && feasible(t, lo - 1)) --lo;
    int side_raw = lo;
    out->side_raw = side_raw;

    /* 边长向上取整到 2 的幂；若取整后反而装不下（不会发生，幂等性保证），继续翻倍。 */
    int side = next_pow2(side_raw);
    out->pow2_rounded = (side != side_raw);
    while (!feasible(t, side)) {
        if (side >= ATLAS_MAX_SIDE) return ATLAS_EINFEASIBLE;
        side <<= 1;
    }

    double t1 = now_ms();
    out->side = side;
    out->packed = 1;
    out->ms = t1 - t0;

    long long side2 = (long long)side * side;
    out->waste = (double)(side2 - area) / (double)side2;
    long long raw2 = (long long)side_raw * side_raw;
    out->waste_raw = (double)(raw2 - area) / (double)raw2;

    /* 最终边长一次性装箱并核对放置数。 */
    static FRect out_rects[ATLAS_MAX_TILES];
    double p0 = now_ms();
    int ok = pack_best(t, side, out_rects);
    double p1 = now_ms();
    out->pack_ms = p1 - p0;
    if (!ok) { out->packed = 0; return ATLAS_EINFEASIBLE; }
    out->boxes = t->n;

    out->pass = (out->waste <= max_waste + 1e-12);
    if (!out->pass) return ATLAS_EWASTE;
    return ATLAS_OK;
}

/* ---------------- 样例 ---------------- */

static void add(Tiles *t, int w, int h, int count) {
    for (int i = 0; i < count && t->n < ATLAS_MAX_TILES; ++i) {
        t->t[t->n].w = w;
        t->t[t->n].h = h;
        t->n++;
    }
}

void atlas_sample_icons(Tiles *t) {
    t->n = 0;
    add(t, 16, 16, 12);
    add(t, 20, 20, 10);
    add(t, 24, 24, 10);
    add(t, 32, 32, 8);
    add(t, 40, 40, 6);
    add(t, 48, 48, 4);
    add(t, 24, 32, 4);
    add(t, 32, 24, 4);
}

void atlas_sample_mixed(Tiles *t) {
    t->n = 0;
    add(t, 160, 128, 1);
    add(t, 128, 160, 1);
    add(t, 96, 64, 2);
    add(t, 64, 96, 2);
    add(t, 80, 80, 2);
    add(t, 112, 48, 2);
    add(t, 48, 112, 2);
    add(t, 48, 48, 8);
    add(t, 32, 32, 12);
    add(t, 24, 24, 12);
    add(t, 16, 16, 12);
}

void atlas_sample_strips(Tiles *t) {
    t->n = 0;
    /* 极端长条：1..8 像素厚，长度 128 或 256，横竖都有。 */
    add(t, 1, 256, 12);
    add(t, 2, 256, 8);
    add(t, 3, 256, 6);
    add(t, 4, 256, 4);
    add(t, 6, 256, 3);
    add(t, 8, 256, 2);
    add(t, 256, 1, 12);
    add(t, 256, 2, 8);
    add(t, 256, 3, 6);
    add(t, 256, 4, 4);
    add(t, 256, 6, 3);
    add(t, 256, 8, 2);
    add(t, 1, 128, 8);
    add(t, 128, 1, 8);
    add(t, 2, 128, 4);
    add(t, 128, 2, 4);
}

void atlas_sample_uniform(Tiles *t) {
    t->n = 0;
    add(t, 32, 32, 63);
}
