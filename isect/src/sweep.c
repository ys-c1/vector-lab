#define _POSIX_C_SOURCE 199309L
#include "sweep.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------- 内部结构 ---------- */

typedef struct {
    double dx, dy;          /* 规范化方向（起点为较小端点） */
    SPoint lo, hi;          /* 按 (x,y) 字典序的两端点 */
} SegGeo;

typedef struct {
    double x, y;
    SEventKind kind;
    int seg_id;
    int seg_a, seg_b;
    int eid;                /* 构建序号，全序的最终兜底（不同事件必不同） */
} Event;

/* ---------- 输入校验 ---------- */

static int same_point(SPoint a, SPoint b) {
    /* 精确相等：NaN 不等于自身，也会被当作非法端点拒掉 */
    return a.x == b.x && a.y == b.y;
}

static int validate(const SSeg *segs, int n, SError *err) {
    if (!segs || n <= 0) {
        if (err) {
            err->code = 3; err->seg_a = -1; err->seg_b = -1;
            snprintf(err->msg, sizeof(err->msg), "invalid argument: segs=%p n=%d",
                     (const void *)segs, n);
        }
        return -1;
    }
    for (int i = 0; i < n; ++i) {
        if (!isfinite(segs[i].p.x) || !isfinite(segs[i].p.y) ||
            !isfinite(segs[i].q.x) || !isfinite(segs[i].q.y)) {
            if (err) {
                err->code = 3; err->seg_a = segs[i].id; err->seg_b = -1;
                snprintf(err->msg, sizeof(err->msg),
                         "segment %d has non-finite endpoint", segs[i].id);
            }
            return -1;
        }
        if (same_point(segs[i].p, segs[i].q)) {
            if (err) {
                err->code = 1; err->seg_a = segs[i].id; err->seg_b = -1;
                snprintf(err->msg, sizeof(err->msg),
                         "zero-length segment %d: endpoints (%.17g, %.17g) duplicated",
                         segs[i].id, segs[i].p.x, segs[i].p.y);
            }
            return -1;
        }
    }
    /* 重复段：端点对（无序）完全相同 */
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            int fwd = same_point(segs[i].p, segs[j].p) && same_point(segs[i].q, segs[j].q);
            int rev = same_point(segs[i].p, segs[j].q) && same_point(segs[i].q, segs[j].p);
            if (fwd || rev) {
                if (err) {
                    err->code = 2; err->seg_a = segs[i].id; err->seg_b = segs[j].id;
                    snprintf(err->msg, sizeof(err->msg),
                             "duplicate segments %d and %d: identical endpoint pair",
                             segs[i].id, segs[j].id);
                }
                return -1;
            }
        }
    }
    return 0;
}

/* ---------- 几何工具 ---------- */

static double cross2(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static SegGeo make_geo(const SSeg *s) {
    SegGeo g;
    int swap = (s->q.x < s->p.x) || (s->q.x == s->p.x && s->q.y < s->p.y);
    g.lo = swap ? s->q : s->p;
    g.hi = swap ? s->p : s->q;
    g.dx = g.hi.x - g.lo.x;
    g.dy = g.hi.y - g.lo.y;   /* dx>=0；竖直段 dx=0,dy>0，斜率 +inf */
    return g;
}

/* 两求交段求交点，返回 0 表示不相交（含平行/共线，与 isect_seg 约定一致） */
static int seg_intersect(const SegGeo *a, const SegGeo *b, double *px, double *py) {
    double rx = a->dx, ry = a->dy;
    double sx = b->dx, sy = b->dy;
    double den = cross2(rx, ry, sx, sy);
    if (den == 0.0) return 0;              /* 平行或共线：无唯一交点，剔除 */
    double qx = b->lo.x - a->lo.x, qy = b->lo.y - a->lo.y;
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    if (t < 0.0 || t > 1.0 || u < 0.0 || u > 1.0) return 0;
    *px = a->lo.x + rx * t;
    *py = a->lo.y + ry * t;
    return 1;
}

/* 斜率序比较：返回负/零/正。叉积判方向，竖直段（dx=0）排最后。 */
static int slope_cmp(const SegGeo *a, const SegGeo *b) {
    int va = a->dx == 0.0, vb = b->dx == 0.0;
    if (va && vb) return 0;                 /* 都是竖直：斜率相等 */
    if (va) return 1;                       /* +inf 最大 */
    if (vb) return -1;
    double c = cross2(a->dx, a->dy, b->dx, b->dy);
    if (c > 0.0) return -1;                 /* a 斜率小（a 在 b 顺时针侧） */
    if (c < 0.0) return 1;
    return 0;
}

/* ---------- 随机数（仅旧路径使用） ---------- */

static unsigned long rng_state;

static unsigned long rng_next(void) {
    /* xorshift32，周期 2^32-1，确定性可复现 */
    unsigned long x = rng_state ? (unsigned long)(uint32_t)rng_state : 0x9e3779b9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    rng_state = x;
    return x;
}

static unsigned long entropy_seed(void) {
    unsigned long s = (unsigned long)time(NULL);
    s ^= (unsigned long)(uintptr_t)&rng_state;
    s ^= (unsigned long)clock();
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
        s ^= (unsigned long)ts.tv_nsec * 2654435761u;
    return s ? s : 1u;
}

/* ---------- 事件比较器 ---------- */

/* 旧的不稳定键：只看位置和类型；同位置同类型即判等，qsort 不保证次序 */
static int cmp_legacy(const void *pa, const void *pb) {
    const Event *a = (const Event *)pa, *b = (const Event *)pb;
    if (a->x < b->x) return -1;
    if (a->x > b->x) return 1;
    if (a->y < b->y) return -1;
    if (a->y > b->y) return 1;
    if (a->kind < b->kind) return -1;
    if (a->kind > b->kind) return 1;
    return 0;
}

/* 新的全序键：位置、类型、斜率序、段号、交点段对，逐级兜底。
 * 重复段已在校验阶段拒绝，因此不同事件绝不会在最后一级仍相等。 */
static int cmp_stable(const void *pa, const void *pb, const SegGeo *geo) {
    const Event *a = (const Event *)pa, *b = (const Event *)pb;
    if (a->x < b->x) return -1;
    if (a->x > b->x) return 1;
    if (a->y < b->y) return -1;
    if (a->y > b->y) return 1;
    if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
    int sc = slope_cmp(&geo[a->seg_id], &geo[b->seg_id]);
    if (sc) return sc;
    if (a->seg_id != b->seg_id) return a->seg_id < b->seg_id ? -1 : 1;
    /* 同位置同类型同段：可能是两个不同的交点事件，先按段对分序 */
    if (a->seg_a != b->seg_a) return a->seg_a < b->seg_a ? -1 : 1;
    if (a->seg_b != b->seg_b) return a->seg_b < b->seg_b ? -1 : 1;
    /* 最终兜底：共端点 RIGHT、同段多交点等残余情形由唯一构建序号分序，
     * 保证任意两个不同事件比较结果非零（严格全序） */
    if (a->eid != b->eid) return a->eid < b->eid ? -1 : 1;
    return 0;
}

/* qsort 不支持上下文参数，用文件内指针传递几何表（模块内调用串行） */
static const SegGeo *cmp_ctx;

static int cmp_stable_wrap(const void *pa, const void *pb) {
    return cmp_stable(pa, pb, cmp_ctx);
}

/* ---------- 事件构建与排序 ---------- */

static int build_events(const SSeg *segs, int n, SegGeo *geo, Event **pev,
                        int *pnevents, SError *err) {
    int cap = 2 * n + n * (n - 1) / 2;
    Event *ev = (Event *)malloc((size_t)cap * sizeof(Event));
    if (!ev) {
        if (err) { err->code = 4; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg), "out of memory"); }
        return -1;
    }
    int m = 0;
    for (int i = 0; i < n; ++i) {
        geo[i] = make_geo(&segs[i]);
        ev[m] = (Event){ geo[i].lo.x, geo[i].lo.y, SEV_LEFT, i, -1, -1, m }; ++m;
        ev[m] = (Event){ geo[i].hi.x, geo[i].hi.y, SEV_RIGHT, i, -1, -1, m }; ++m;
    }
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            double px, py;
            if (seg_intersect(&geo[i], &geo[j], &px, &py)) {
                int a = segs[i].id, b = segs[j].id;
                if (a > b) { int t = a; a = b; b = t; }
                ev[m] = (Event){ px, py, SEV_CROSS, i, a, b, m }; ++m;
            }
        }
    }
    *pev = ev;
    *pnevents = m;
    return 0;
}

static int sort_events(Event *ev, int m, const SegGeo *geo,
                       int stable, long seed) {
    if (stable) {
        cmp_ctx = geo;
        qsort(ev, (size_t)m, sizeof(Event), cmp_stable_wrap);
        /* 断言全序：相邻事件不得相等 */
        for (int i = 1; i < m; ++i)
            if (cmp_stable(&ev[i - 1], &ev[i], geo) == 0) return -2;
        return 0;
    }
    /* 旧路径：先洗牌再 qsort。qsort 对比较相等的元素无顺序保证，
     * 洗牌让相等块的相对次序随种子变化，复现“同输入不同顺序”。 */
    rng_state = seed >= 0 ? (unsigned long)seed : entropy_seed();
    for (int i = m - 1; i > 0; --i) {
        int k = (int)(rng_next() % (unsigned long)(i + 1));
        Event t = ev[i]; ev[i] = ev[k]; ev[k] = t;
    }
    qsort(ev, (size_t)m, sizeof(Event), cmp_legacy);
    return 0;
}

/* ---------- 扫描线：按事件顺序产出交点表 ---------- */

static int active_index(const int *active, int na, int id) {
    for (int i = 0; i < na; ++i) if (active[i] == id) return i;
    return -1;
}

static int run_sweep(const Event *ev, int m, int n, STable *out, SError *err) {
    int *active = (int *)malloc((size_t)n * sizeof(int));
    /* 事件总数 m 是交点数的天然上界（每事件至多产出一条交点行） */
    SHit *hits = (SHit *)malloc((size_t)(m > 0 ? m : 1) * sizeof(SHit));
    if (!active || !hits) {
        free(active); free(hits);
        if (err) { err->code = 4; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg), "out of memory"); }
        return -1;
    }
    int na = 0, nh = 0;
    for (int k = 0; k < m; ++k) {
        const Event *e = &ev[k];
        if (e->kind == SEV_LEFT) {
            active[na++] = e->seg_id;
        } else if (e->kind == SEV_RIGHT) {
            int idx = active_index(active, na, e->seg_id);
            if (idx >= 0) { active[idx] = active[--na]; }
        } else {
            /* CROSS：记录交点；两端点相接（T 型/共端点）也由求交阶段收录 */
            (void)active;
            hits[nh++] = (SHit){ { e->x, e->y }, e->seg_a, e->seg_b };
        }
    }
    free(active);
    out->hits = hits;
    out->count = nh;
    return 0;
}

/* ---------- 公开入口 ---------- */

int sweep_table(const SSeg *segs, int n, int stable, long seed,
                STable *out, SError *err) {
    if (err) memset(err, 0, sizeof(*err));
    if (!out) {
        if (err) { err->code = 3; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg), "out is NULL"); }
        return -1;
    }
    out->hits = 0; out->count = 0;
    if (validate(segs, n, err) != 0) return -1;

    SegGeo *geo = (SegGeo *)malloc((size_t)n * sizeof(SegGeo));
    Event *ev = 0; int m = 0;
    if (!geo || build_events(segs, n, geo, &ev, &m, err) != 0) {
        free(geo);
        if (err && err->code == 0) { err->code = 4;
            snprintf(err->msg, sizeof(err->msg), "out of memory"); }
        return -1;
    }
    int rc = sort_events(ev, m, geo, stable, seed);
    if (rc == -2) {
        free(geo); free(ev);
        if (err) { err->code = 4; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg),
                            "total order violated: two events share a full key"); }
        return -1;
    }
    rc = run_sweep(ev, m, n, out, err);
    free(geo);
    free(ev);
    return rc;
}

int sweep_events(const SSeg *segs, int n, int stable, long seed,
                 SEventList *out, SError *err) {
    if (err) memset(err, 0, sizeof(*err));
    if (!out) {
        if (err) { err->code = 3; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg), "out is NULL"); }
        return -1;
    }
    out->events = 0; out->count = 0;
    if (validate(segs, n, err) != 0) return -1;

    SegGeo *geo = (SegGeo *)malloc((size_t)n * sizeof(SegGeo));
    Event *ev = 0; int m = 0;
    if (!geo || build_events(segs, n, geo, &ev, &m, err) != 0) {
        free(geo); free(ev);
        if (err && err->code == 0) { err->code = 4;
            snprintf(err->msg, sizeof(err->msg), "out of memory"); }
        return -1;
    }
    int rc = sort_events(ev, m, geo, stable, seed);
    if (rc == -2) {
        free(geo); free(ev);
        if (err) { err->code = 4; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg),
                            "total order violated: two events share a full key"); }
        return -1;
    }
    SEvent *pub = (SEvent *)malloc((size_t)m * sizeof(SEvent));
    if (!pub) {
        free(geo); free(ev);
        if (err) { err->code = 4; err->seg_a = err->seg_b = -1;
                   snprintf(err->msg, sizeof(err->msg), "out of memory"); }
        return -1;
    }
    for (int i = 0; i < m; ++i)
        pub[i] = (SEvent){ ev[i].x, ev[i].y, ev[i].kind,
                           ev[i].seg_id, ev[i].seg_a, ev[i].seg_b };
    free(geo); free(ev);
    out->events = pub; out->count = m;
    return 0;
}

void sweep_free_table(STable *t) { if (t) { free(t->hits); t->hits = 0; t->count = 0; } }
void sweep_free_events(SEventList *e) { if (e) { free(e->events); e->events = 0; e->count = 0; } }

const char *sweep_strerror(const SError *err) {
    return (err && err->msg[0]) ? err->msg : "ok";
}
