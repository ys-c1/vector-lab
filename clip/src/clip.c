#include "clip.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CLIP_TWO_PI 6.283185307179586476925286766559
#define CLIP_EPS 1e-7
#define CLIP_AREA_EPS 1e-9
#define CLIP_CELL_CAP 512
#define CLIP_CELL_MAX 1024

typedef struct { int count; CPoint pts[CLIP_CELL_CAP]; } Cell;
typedef struct { CPoint a, b; int ia, ib, used, dead; } Seg;

static int inside(CPoint a, CPoint b, CPoint p) {
    double cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return cr >= 0.0;
}

static CPoint meet(CPoint a, CPoint b, CPoint c, CPoint d) {
    double a1 = b.y - a.y, b1 = a.x - b.x, c1 = a1 * a.x + b1 * a.y;
    double a2 = d.y - c.y, b2 = c.x - d.x, c2 = a2 * c.x + b2 * c.y;
    double det = a1 * b2 - a2 * b1;
    CPoint r = {0, 0};
    if (det == 0.0) return r;
    r.x = (b2 * c1 - b1 * c2) / det;
    r.y = (a1 * c2 - a2 * c1) / det;
    return r;
}

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
    CPoly cur, nxt;
    cur.count = n;
    for (int i = 0; i < n; ++i) cur.pts[i] = poly[i];
    for (int e = 0; e < wn; ++e) {
        CPoint a = win[e], b = win[(e + 1) % wn];
        nxt.count = 0;
        for (int i = 0; i < cur.count; ++i) {
            CPoint p = cur.pts[i], q = cur.pts[(i + 1) % cur.count];
            int ip = inside(a, b, p), iq = inside(a, b, q);
            if (ip) {
                if (nxt.count < 62) nxt.pts[nxt.count++] = p;
            }
            if (ip != iq) {
                CPoint m = meet(p, q, a, b);
                if (nxt.count < 62) nxt.pts[nxt.count++] = m;
            }
        }
        cur = nxt;
        if (cur.count == 0) break;
    }
    out->count = cur.count;
    for (int i = 0; i < cur.count; ++i) out->pts[i] = cur.pts[i];
    return 0;
}

double clip_area(const CPoly *p) {
    if (!p || p->count < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < p->count; ++i) {
        CPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

/* ------------------------------------------------------------------ */
/* small geometry helpers                                              */
/* ------------------------------------------------------------------ */

static double orient(CPoint a, CPoint b, CPoint c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

static double ring_area_raw(const CPoint *p, int n) {
    if (!p || n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

static int pt_eq(CPoint a, CPoint b) {
    return fabs(a.x - b.x) <= CLIP_EPS && fabs(a.y - b.y) <= CLIP_EPS;
}

static int on_seg(CPoint a, CPoint b, CPoint p) {
    if (fabs(orient(a, b, p)) > CLIP_EPS) return 0;
    double lo, hi;
    lo = a.x < b.x ? a.x : b.x; hi = a.x > b.x ? a.x : b.x;
    if (p.x < lo - CLIP_EPS || p.x > hi + CLIP_EPS) return 0;
    lo = a.y < b.y ? a.y : b.y; hi = a.y > b.y ? a.y : b.y;
    if (p.y < lo - CLIP_EPS || p.y > hi + CLIP_EPS) return 0;
    return 1;
}

static int seg_hit(CPoint a, CPoint b, CPoint c, CPoint d) {
    double o1 = orient(a, b, c), o2 = orient(a, b, d);
    double o3 = orient(c, d, a), o4 = orient(c, d, b);
    if (((o1 > CLIP_EPS && o2 < -CLIP_EPS) || (o1 < -CLIP_EPS && o2 > CLIP_EPS)) &&
        ((o3 > CLIP_EPS && o4 < -CLIP_EPS) || (o3 < -CLIP_EPS && o4 > CLIP_EPS)))
        return 1;
    if (fabs(o1) <= CLIP_EPS && on_seg(a, b, c)) return 1;
    if (fabs(o2) <= CLIP_EPS && on_seg(a, b, d)) return 1;
    if (fabs(o3) <= CLIP_EPS && on_seg(c, d, a)) return 1;
    if (fabs(o4) <= CLIP_EPS && on_seg(c, d, b)) return 1;
    return 0;
}

static int ring_self_intersects(const CPoint *p, int n) {
    for (int i = 0; i < n; ++i) {
        int i2 = (i + 1) % n;
        for (int j = i + 1; j < n; ++j) {
            int j2 = (j + 1) % n;
            if (j == i2 || j2 == i) continue;
            if (seg_hit(p[i], p[i2], p[j], p[j2])) return 1;
        }
    }
    return 0;
}

/* 1 inside, 0 on the boundary, -1 outside */
static int pt_loc(const CPoint *r, int n, CPoint p) {
    int in = 0;
    for (int i = 0; i < n; ++i) {
        CPoint a = r[i], b = r[(i + 1) % n];
        if (on_seg(a, b, p)) return 0;
        if ((a.y > p.y) != (b.y > p.y)) {
            double xint = (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x;
            if (p.x < xint) in = !in;
        }
    }
    return in ? 1 : -1;
}

static int pts_dedupe(CPoint *p, int n) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        if (m == 0 || !pt_eq(p[m - 1], p[i])) p[m++] = p[i];
    }
    while (m > 1 && pt_eq(p[0], p[m - 1])) --m;
    return m;
}

/* Drop duplicate points and empty/degenerate rings.  Returns the new count,
 * or 0 when the ring carries no area. */
static int clean_poly(CPoint *p, int n) {
    n = pts_dedupe(p, n);
    if (n < 3) return 0;
    if (fabs(ring_area_raw(p, n)) < CLIP_AREA_EPS) return 0;
    return n;
}

static int copy_ccw(const CPoint *p, int n, CPoint *out, int cap) {
    if (n > cap) return -1;
    for (int i = 0; i < n; ++i) out[i] = p[i];
    if (ring_area_raw(out, n) < 0.0) {
        for (int i = 0; i < n / 2; ++i) {
            CPoint t = out[i]; out[i] = out[n - 1 - i]; out[n - 1 - i] = t;
        }
    }
    return n;
}

/* Clip a ring by the half-plane left of a->b (keep_left) or right of it. */
static int clip_half(const CPoint *in, int n, CPoint a, CPoint b,
                     int keep_left, CPoint *out, int cap) {
    int m = 0;
    for (int i = 0; i < n; ++i) {
        CPoint p = in[i], q = in[(i + 1) % n];
        double vp = orient(a, b, p), vq = orient(a, b, q);
        if (!keep_left) { vp = -vp; vq = -vq; }
        int ip = vp >= 0.0, iq = vq >= 0.0;
        if (ip) {
            if (m >= cap) return -1;
            out[m++] = p;
        }
        if (ip != iq) {
            double den = vp - vq;
            double t = den != 0.0 ? vp / den : 0.0;
            if (m >= cap) return -1;
            out[m].x = p.x + (q.x - p.x) * t;
            out[m].y = p.y + (q.y - p.y) * t;
            ++m;
        }
    }
    return m;
}

/* ------------------------------------------------------------------ */
/* hole validation                                                     */
/* ------------------------------------------------------------------ */

int clip_hole_status(const CPoint *win, int wn, const CPoint *hole, int hn) {
    if (!win || wn < 3) return CLIP_ERR_ARGS;
    if (!hole || hn < 3) return CLIP_ERR_HOLE_DEGENERATE;
    for (int i = 0; i < hn; ++i) {
        if (pt_eq(hole[i], hole[(i + 1) % hn])) return CLIP_ERR_HOLE_DEGENERATE;
    }
    if (fabs(ring_area_raw(hole, hn)) < CLIP_AREA_EPS)
        return CLIP_ERR_HOLE_DEGENERATE;
    if (ring_self_intersects(hole, hn))
        return CLIP_ERR_HOLE_DEGENERATE;
    for (int i = 0; i < hn; ++i) {
        if (pt_loc(win, wn, hole[i]) != 1) return CLIP_ERR_HOLE_OUTSIDE;
    }
    for (int i = 0; i < hn; ++i) {
        CPoint a = hole[i], b = hole[(i + 1) % hn];
        for (int j = 0; j < wn; ++j) {
            CPoint c = win[j], d = win[(j + 1) % wn];
            if (seg_hit(a, b, c, d)) return CLIP_ERR_HOLE_OUTSIDE;
        }
    }
    return CLIP_OK;
}

/* ------------------------------------------------------------------ */
/* cell decomposition                                                  */
/* ------------------------------------------------------------------ */

static int ring_is_convex_ccw(const CPoint *p, int n) {
    int sign = 0;
    for (int i = 0; i < n; ++i) {
        double cr = orient(p[i], p[(i + 1) % n], p[(i + 2) % n]);
        if (fabs(cr) <= CLIP_AREA_EPS) continue;
        int s = cr > 0.0 ? 1 : -1;
        if (sign && s != sign) return 0;
        sign = s;
    }
    return sign == 1;
}

/* A point on the triangle boundary blocks the ear too: a valid ear diagonal
 * must not pass through any other vertex. */
static int pt_in_tri_strict(CPoint p, CPoint a, CPoint b, CPoint c) {
    return orient(a, b, p) >= -CLIP_AREA_EPS &&
           orient(b, c, p) >= -CLIP_AREA_EPS &&
           orient(c, a, p) >= -CLIP_AREA_EPS;
}

/* Ear clipping.  poly must be CCW, simple and non-degenerate.  Writes at
 * most 62 triangles. */
static int ear_clip(const CPoint *poly, int n, CPoint tris[][3]) {
    if (n > 64) return -1;
    CPoint v[64];
    int idx[64];
    for (int i = 0; i < n; ++i) { v[i] = poly[i]; idx[i] = i; }
    int m = n;
    /* Drop collinear vertices; they only create zero-area ears. */
    for (int i = 0; i < m; ) {
        int ia = idx[(i + m - 1) % m], ib = idx[i], ic = idx[(i + 1) % m];
        if (fabs(orient(v[ia], v[ib], v[ic])) <= CLIP_AREA_EPS) {
            for (int k = i; k < m - 1; ++k) idx[k] = idx[k + 1];
            --m;
            if (i >= m) i = 0;
        } else {
            ++i;
        }
    }
    if (m < 3) return -1;
    int nt = 0, guard = 0;
    while (m > 3) {
        if (++guard > m * m + 64) return -1;
        int ear = -1;
        for (int i = 0; i < m; ++i) {
            int ia = idx[(i + m - 1) % m];
            int ib = idx[i];
            int ic = idx[(i + 1) % m];
            if (orient(v[ia], v[ib], v[ic]) <= CLIP_AREA_EPS) continue;
            int ok = 1;
            for (int j = 0; j < m; ++j) {
                int jj = idx[j];
                if (jj == ia || jj == ib || jj == ic) continue;
                if (pt_in_tri_strict(v[jj], v[ia], v[ib], v[ic])) {
                    ok = 0;
                    break;
                }
            }
            if (ok) { ear = i; break; }
        }
        if (ear < 0) return -1;
        int ia = idx[(ear + m - 1) % m];
        int ib = idx[ear];
        int ic = idx[(ear + 1) % m];
        tris[nt][0] = v[ia];
        tris[nt][1] = v[ib];
        tris[nt][2] = v[ic];
        ++nt;
        for (int k = ear; k < m - 1; ++k) idx[k] = idx[k + 1];
        --m;
    }
    tris[nt][0] = v[idx[0]];
    tris[nt][1] = v[idx[1]];
    tris[nt][2] = v[idx[2]];
    return nt + 1;
}

static int cell_bbox(const Cell *c, double *minx, double *miny,
                     double *maxx, double *maxy) {
    if (!c || c->count == 0) return 0;
    *minx = *maxx = c->pts[0].x;
    *miny = *maxy = c->pts[0].y;
    for (int i = 1; i < c->count; ++i) {
        if (c->pts[i].x < *minx) *minx = c->pts[i].x;
        if (c->pts[i].x > *maxx) *maxx = c->pts[i].x;
        if (c->pts[i].y < *miny) *miny = c->pts[i].y;
        if (c->pts[i].y > *maxy) *maxy = c->pts[i].y;
    }
    return 1;
}

/* Separating-axis test, sufficient for the bbox pre-pass below. */
static int bbox_overlap(double aminx, double aminy, double amaxx, double amaxy,
                        double bminx, double bminy, double bmaxx, double bmaxy) {
    return !(amaxx < bminx - CLIP_EPS || bmaxx < aminx - CLIP_EPS ||
             amaxy < bminy - CLIP_EPS || bmaxy < aminy - CLIP_EPS);
}

static int add_cell(Cell *list, int *count, const CPoint *pts, int n) {
    if (*count >= CLIP_CELL_MAX) return -1;
    Cell *c = &list[*count];
    c->count = n;
    for (int i = 0; i < n; ++i) c->pts[i] = pts[i];
    ++*count;
    return 0;
}

/* Replace src by src minus the convex CCW ring sub, writing convex pieces
 * into dst.  This is the "reverse clip": for every edge of sub the part that
 * falls outside that edge is emitted, and what remains inside all edges is
 * the hole itself and is dropped. */
static int subtract_convex(Cell *dst, int *ndst, const Cell *src,
                           const CPoint *sub, int sn) {
    /* Room for one extra crossing vertex on top of the source ring. */
    CPoint work[CLIP_CELL_CAP], out[CLIP_CELL_CAP], in[CLIP_CELL_CAP];
    int n = src->count;
    memcpy(work, src->pts, sizeof(CPoint) * (size_t)n);
    for (int e = 0; e < sn; ++e) {
        CPoint a = sub[e], b = sub[(e + 1) % sn];
        int m = clip_half(work, n, a, b, 0, out, CLIP_CELL_CAP);
        if (m < 0) return -1;
        m = clean_poly(out, m);
        if (m > 0 && add_cell(dst, ndst, out, m) != 0) return -1;

        m = clip_half(work, n, a, b, 1, in, CLIP_CELL_CAP);
        if (m < 0) return -1;
        m = clean_poly(in, m);
        n = m;
        if (n > 0) memcpy(work, in, sizeof(CPoint) * (size_t)n);
        if (n == 0) break;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* boundary assembly                                                   */
/* ------------------------------------------------------------------ */

/* Map a point to a canonical vertex id through a sorted vertex table. */
typedef struct { CPoint p; int id; } VRef;

static int vref_cmp(const void *a, const void *b) {
    const VRef *x = (const VRef *)a, *y = (const VRef *)b;
    if (x->p.x < y->p.x) return -1;
    if (x->p.x > y->p.x) return 1;
    if (x->p.y < y->p.y) return -1;
    if (x->p.y > y->p.y) return 1;
    return 0;
}

static long vref_lower(const VRef *v, long n, double x) {
    long lo = 0, hi = n;
    while (lo < hi) {
        long mid = lo + (hi - lo) / 2;
        if (v[mid].p.x < x - CLIP_EPS) lo = mid + 1;
        else hi = mid;
    }
    return lo;
}

static int vref_find(const VRef *v, long n, CPoint p) {
    long i = vref_lower(v, n, p.x);
    int best = -1;
    double bestd = 0.0;
    for (long j = i; j < n; ++j) {
        if (v[j].p.x > p.x + CLIP_EPS) break;
        double dx = v[j].p.x - p.x, dy = v[j].p.y - p.y;
        if (fabs(dx) <= CLIP_EPS && fabs(dy) <= CLIP_EPS) {
            double d = dx * dx + dy * dy;
            if (best < 0 || d < bestd) { best = v[j].id; bestd = d; }
        }
    }
    return best;
}

/* Hash an ordered pair of vertex ids. */
static unsigned long long edge_key(int a, int b) {
    return ((unsigned long long)(unsigned)a << 32) | (unsigned)b;
}

typedef struct {
    unsigned long long *key;
    int *seg;
    unsigned char *occ;
    long cap, count;
} EdgeTable;

static int etab_init(EdgeTable *t, long want) {
    t->cap = 16;
    while (t->cap < want * 2) t->cap <<= 1;
    t->key = (unsigned long long *)calloc((size_t)t->cap, sizeof(unsigned long long));
    t->seg = (int *)calloc((size_t)t->cap, sizeof(int));
    t->occ = (unsigned char *)calloc((size_t)t->cap, sizeof(unsigned char));
    t->count = 0;
    return (t->key && t->seg && t->occ) ? 0 : -1;
}

static void etab_free(EdgeTable *t) {
    free(t->key); free(t->seg); free(t->occ);
    t->key = NULL; t->seg = NULL; t->occ = NULL;
}

/* Insert an edge; an existing reversed twin marks both segments dead.
 * Returns 0 on success, -1 on allocation failure. */
static int etab_add(EdgeTable *t, Seg *segs, int idx) {
    unsigned long long k = edge_key(segs[idx].ia, segs[idx].ib);
    unsigned long long h = k * 0x9E3779B97F4A7C15ULL;
    long mask = t->cap - 1;
    long pos = (long)(h >> 7) & mask;
    while (t->occ[pos]) {
        if (t->key[pos] == k) return 0; /* same direction: boundary self-overlap */
        pos = (pos + 1) & mask;
    }
    t->occ[pos] = 1;
    t->key[pos] = k;
    t->seg[pos] = idx;
    ++t->count;
    return 0;
}

/* Remove the segment whose key equals the reversed edge.  Returns its index
 * or -1.  The slot is cleared by backward-shift deletion. */
static int etab_take_reverse(EdgeTable *t, int a, int b) {
    unsigned long long k = edge_key(b, a);
    unsigned long long h = k * 0x9E3779B97F4A7C15ULL;
    long mask = t->cap - 1;
    long pos = (long)(h >> 7) & mask;
    while (t->occ[pos]) {
        if (t->key[pos] == k) {
            int found = t->seg[pos];
            t->occ[pos] = 0;
            /* backward-shift: pull later entries of the probe chain back */
            long gap = pos;
            long p = (pos + 1) & mask;
            while (t->occ[p]) {
                unsigned long long hh = t->key[p] * 0x9E3779B97F4A7C15ULL;
                long home = (long)(hh >> 7) & mask;
                /* is `home` in the cyclic interval (gap, p]? */
                int in_interval;
                if (gap <= p) in_interval = (home > gap && home <= p);
                else in_interval = (home > gap || home <= p);
                if (!in_interval) {
                    t->occ[gap] = 1;
                    t->key[gap] = t->key[p];
                    t->seg[gap] = t->seg[p];
                    t->occ[p] = 0;
                    gap = p;
                }
                p = (p + 1) & mask;
            }
            --t->count;
            return found;
        }
        pos = (pos + 1) & mask;
    }
    return -1;
}

/* Build the canonical vertex table and the split, de-duplicated edge set.
 * Every edge is first cut at each other vertex lying on it, so edges shared
 * by two pieces become identical and can be cancelled; what remains is the
 * boundary of the union.  Live edges are compacted into the front of `segs`;
 * their count is returned through *live. */
static Seg *build_boundary(const Cell *cells, int ncell, long *live) {
    *live = 0;
    long nv_cap = 0, seg_cap = 0;
    for (int i = 0; i < ncell; ++i) {
        nv_cap += cells[i].count;
        seg_cap += cells[i].count;
    }
    if (nv_cap == 0) return NULL;

    VRef *verts = (VRef *)malloc(sizeof(VRef) * (size_t)nv_cap);
    if (!verts) return NULL;
    long nv = 0;
    for (int i = 0; i < ncell; ++i)
        for (int j = 0; j < cells[i].count; ++j) {
            verts[nv].p = cells[i].pts[j];
            verts[nv].id = (int)nv;
            ++nv;
        }
    qsort(verts, (size_t)nv, sizeof(VRef), vref_cmp);
    long cu = 0;
    for (long i = 0; i < nv; ++i) {
        if (cu > 0 && fabs(verts[i].p.x - verts[cu - 1].p.x) <= CLIP_EPS &&
            fabs(verts[i].p.y - verts[cu - 1].p.y) <= CLIP_EPS)
            continue;
        verts[cu++] = verts[i];
        verts[cu - 1].id = (int)(cu - 1);
    }
    nv = cu;

    /* Splits are rare in practice: the coordinate spans of a piece are
     * disjoint, so only a handful of vertices can lie on any one edge.  A
     * linear bound is enough and avoids a huge lazily-zeroed array. */
    seg_cap = seg_cap * 4 + 64;
    Seg *segs = (Seg *)malloc((size_t)seg_cap * sizeof(Seg));
    long *ts = (long *)malloc(sizeof(long) * (size_t)(nv + 1));
    if (!segs || !ts) { free(segs); free(ts); free(verts); return NULL; }
    long k = 0;
    int bad = 0;

    for (int i = 0; i < ncell && !bad; ++i) {
        const Cell *c = &cells[i];
        for (int e = 0; e < c->count && !bad; ++e) {
            CPoint a = c->pts[e], b = c->pts[(e + 1) % c->count];
            if (pt_eq(a, b)) continue;
            double dx = b.x - a.x, dy = b.y - a.y;
            double len2 = dx * dx + dy * dy;
            double xlo = a.x < b.x ? a.x : b.x, xhi = a.x > b.x ? a.x : b.x;
            int nt = 0;
            for (long vi = vref_lower(verts, nv, xlo);
                 vi < nv && verts[vi].p.x <= xhi + CLIP_EPS; ++vi) {
                CPoint q = verts[vi].p;
                if (pt_eq(q, a) || pt_eq(q, b)) continue;
                if (!on_seg(a, b, q)) continue;
                double t = ((q.x - a.x) * dx + (q.y - a.y) * dy) / len2;
                if (t <= 0.0 || t >= 1.0) continue;
                long key = (long)floor(t / 1e-9 + 0.5);
                int seen = 0;
                for (int z = 0; z < nt; ++z)
                    if (ts[z] == key) { seen = 1; break; }
                if (!seen) ts[nt++] = key;
            }
            for (int x = 1; x < nt; ++x) {
                long key = ts[x];
                int y = x - 1;
                while (y >= 0 && ts[y] > key) { ts[y + 1] = ts[y]; --y; }
                ts[y + 1] = key;
            }
            double t0 = 0.0;
            for (int x = 0; x <= nt; ++x) {
                double t1 = x < nt ? (double)ts[x] * 1e-9 : 1.0;
                if (k >= seg_cap) {
                    /* grow rather than fail: the bound above is a guess */
                    long ncap = seg_cap * 2;
                    Seg *grown = (Seg *)realloc(segs, (size_t)ncap * sizeof(Seg));
                    if (!grown) { bad = 1; break; }
                    segs = grown;
                    seg_cap = ncap;
                }
                segs[k].a.x = a.x + dx * t0;
                segs[k].a.y = a.y + dy * t0;
                segs[k].b.x = a.x + dx * t1;
                segs[k].b.y = a.y + dy * t1;
                segs[k].ia = vref_find(verts, nv, segs[k].a);
                segs[k].ib = vref_find(verts, nv, segs[k].b);
                segs[k].used = segs[k].dead = 0;
                if (segs[k].ia < 0 || segs[k].ib < 0 || segs[k].ia == segs[k].ib) {
                    bad = 1;
                    break;
                }
                ++k;
                t0 = t1;
            }
        }
    }
    free(ts);
    free(verts);
    if (bad) { free(segs); return NULL; }

    /* Cancel opposite duplicates: an edge is either matched against a
     * reversed twin already in the table (both die) or stored for a later
     * twin to find. */
    EdgeTable t;
    if (etab_init(&t, k) != 0) { etab_free(&t); free(segs); return NULL; }
    for (long i = 0; i < k; ++i) {
        int twin = etab_take_reverse(&t, segs[i].ia, segs[i].ib);
        if (twin >= 0) {
            segs[i].dead = 1;
            segs[twin].dead = 1;
        } else if (etab_add(&t, segs, (int)i) != 0) {
            etab_free(&t);
            free(segs);
            return NULL;
        }
    }
    etab_free(&t);

    long m = 0;
    for (long i = 0; i < k; ++i)
        if (!segs[i].dead) segs[m++] = segs[i];
    *live = m;
    return segs;
}
/* Assemble boundary segments into closed rings.  The next segment is chosen
 * by the smallest clockwise turn from the reversed incoming edge, which
 * keeps rings simple even where they touch at a vertex. */
static int assemble_rings(Seg *segs, long m, CPolyRings *out) {
    /* bucket segment indices by start vertex id */
    int maxid = 0;
    for (long i = 0; i < m; ++i)
        if (segs[i].ia > maxid) maxid = segs[i].ia;
    long *start_cnt = (long *)calloc((size_t)maxid + 2, sizeof(long));
    if (!start_cnt) return CLIP_ERR_CAPACITY;
    for (long i = 0; i < m; ++i) ++start_cnt[segs[i].ia];
    long *start_off = (long *)calloc((size_t)maxid + 2, sizeof(long));
    if (!start_off) { free(start_cnt); return CLIP_ERR_CAPACITY; }
    for (int v = 0; v <= maxid; ++v)
        start_off[v + 1] = start_off[v] + start_cnt[v];
    long *by_start = (long *)malloc(sizeof(long) * (size_t)(m > 0 ? m : 1));
    if (!by_start) { free(start_cnt); free(start_off); return CLIP_ERR_CAPACITY; }
    long *fill = (long *)malloc(sizeof(long) * (size_t)(maxid + 2));
    if (!fill) { free(start_cnt); free(start_off); free(by_start); return CLIP_ERR_CAPACITY; }
    for (int v = 0; v <= maxid; ++v) fill[v] = start_off[v];
    for (long i = 0; i < m; ++i) by_start[fill[segs[i].ia]++] = i;

    for (long i = 0; i < m; ++i) {
        if (segs[i].used || segs[i].dead) continue;
        CRing ring;
        ring.count = 0;
        int start = segs[i].ia;
        int cur = (int)i;
        segs[cur].used = 1;
        ring.pts[ring.count++] = segs[cur].a;
        int guard = 0;
        while (guard++ <= m) {
            int to = segs[cur].ib;
            if (to == start) break;
            if (ring.count >= CLIP_RING_CAP) {
                free(start_cnt); free(start_off); free(by_start); free(fill);
                return CLIP_ERR_CAPACITY;
            }
            ring.pts[ring.count++] = segs[cur].b;
            /* choose among edges leaving `to`, clockwise-most from the
             * reverse of the edge we arrived on */
            int best = -1;
            for (long q = start_off[to]; q < start_off[to + 1]; ++q) {
                long cand = by_start[q];
                if (segs[cand].used || segs[cand].dead) continue;
                double d = 0.0;
                if (best >= 0) {
                    double revx = segs[cur].a.x - segs[cur].b.x;
                    double revy = segs[cur].a.y - segs[cur].b.y;
                    double cx = segs[cand].b.x - segs[cand].a.x;
                    double cy = segs[cand].b.y - segs[cand].a.y;
                    double bx = segs[best].b.x - segs[best].a.x;
                    double by = segs[best].b.y - segs[best].a.y;
                    /* signed angle from rev to each candidate, in (0, 2pi] */
                    double an = atan2(revx * cy - revy * cx, revx * cx + revy * cy);
                    double ab = atan2(revx * by - revy * bx, revx * bx + revy * by);
                    if (an <= 1e-12) an += CLIP_TWO_PI;
                    if (ab <= 1e-12) ab += CLIP_TWO_PI;
                    d = an - ab;
                }
                if (best < 0 || d < -1e-12) best = (int)cand;
            }
            if (best < 0) break;
            cur = best;
            segs[cur].used = 1;
        }
        int n = pts_dedupe(ring.pts, ring.count);
        if (n < 3) continue;
        double a = ring_area_raw(ring.pts, n);
        if (fabs(a) < CLIP_AREA_EPS) continue;
        ring.count = n;
        if (a > 0.0) {
            if (out->outer_count >= CLIP_MAX_RINGS) {
                free(start_cnt); free(start_off); free(by_start); free(fill);
                return CLIP_ERR_CAPACITY;
            }
            out->outer[out->outer_count++] = ring;
        } else {
            if (out->hole_count >= CLIP_MAX_RINGS) {
                free(start_cnt); free(start_off); free(by_start); free(fill);
                return CLIP_ERR_CAPACITY;
            }
            out->hole[out->hole_count++] = ring;
        }
    }
    free(start_cnt);
    free(start_off);
    free(by_start);
    free(fill);
    return CLIP_OK;
}

/* ------------------------------------------------------------------ */
/* public entry point                                                  */
/* ------------------------------------------------------------------ */
/* public entry point                                                  */
/* ------------------------------------------------------------------ */

int clip_convex_holes(const CPoint *poly, int n,
                      const CPoint *win, int wn,
                      const CPoly *holes, int nhole,
                      CPolyRings *out, int *bad_hole) {
    if (bad_hole) *bad_hole = -1;
    if (!poly || !win || !out || n < 3 || wn < 3 || nhole < 0) return CLIP_ERR_ARGS;
    if (nhole > 0 && !holes) return CLIP_ERR_ARGS;
    if (nhole > CLIP_MAX_HOLES) return CLIP_ERR_CAPACITY;
    /* Only the headers matter for a well-formed result; zeroing the whole
     * ring array would wipe ~256 KB on every call. */
    out->outer_count = 0;
    out->hole_count = 0;

    /* No holes: keep the historical result bit-for-bit. */
    if (nhole == 0) {
        CPoly tmp;
        int rc = clip_convex(poly, n, win, wn, &tmp);
        if (rc != 0) return rc;
        if (tmp.count >= 3) {
            out->outer_count = 1;
            out->outer[0].count = tmp.count;
            for (int i = 0; i < tmp.count; ++i)
                out->outer[0].pts[i] = tmp.pts[i];
        }
        return CLIP_OK;
    }

    CPoint w[64];
    int wc = copy_ccw(win, wn, w, 64);
    if (wc < 0) return CLIP_ERR_CAPACITY;

    for (int h = 0; h < nhole; ++h) {
        int st = clip_hole_status(w, wc, holes[h].pts, holes[h].count);
        if (st != CLIP_OK) {
            if (bad_hole) *bad_hole = h;
            return st;
        }
    }

    Cell *cells = (Cell *)calloc(CLIP_CELL_MAX, sizeof(Cell));
    Cell *next = (Cell *)calloc(CLIP_CELL_MAX, sizeof(Cell));
    if (!cells || !next) { free(cells); free(next); return CLIP_ERR_CAPACITY; }

    CPoly base;
    if (clip_convex(poly, n, w, wc, &base) != 0) {
        free(cells); free(next);
        return CLIP_ERR_ARGS;
    }
    int ncells = 0;
    if (base.count >= 3 && fabs(clip_area(&base)) >= CLIP_AREA_EPS) {
        if (add_cell(cells, &ncells, base.pts, base.count) != 0) {
            free(cells); free(next);
            return CLIP_ERR_CAPACITY;
        }
    }

    Cell *cur = cells, *aux = next;
    int ncur = ncells;

    for (int h = 0; h < nhole && ncur > 0; ++h) {
        CPoint hp[64];
        int hn = copy_ccw(holes[h].pts, holes[h].count, hp, 64);
        if (hn < 0) { free(cells); free(next); return CLIP_ERR_CAPACITY; }

        double hminx = hp[0].x, hminy = hp[0].y, hmaxx = hp[0].x, hmaxy = hp[0].y;
        for (int z = 1; z < hn; ++z) {
            if (hp[z].x < hminx) hminx = hp[z].x;
            if (hp[z].x > hmaxx) hmaxx = hp[z].x;
            if (hp[z].y < hminy) hminy = hp[z].y;
            if (hp[z].y > hmaxy) hmaxy = hp[z].y;
        }
        if (ring_is_convex_ccw(hp, hn)) {
            int naux = 0;
            for (int i = 0; i < ncur; ++i) {
                double cminx, cminy, cmaxx, cmaxy;
                if (cell_bbox(&cur[i], &cminx, &cminy, &cmaxx, &cmaxy) &&
                    !bbox_overlap(cminx, cminy, cmaxx, cmaxy,
                                  hminx, hminy, hmaxx, hmaxy)) {
                    if (add_cell(aux, &naux, cur[i].pts, cur[i].count) != 0) {
                        free(cells); free(next);
                        return CLIP_ERR_CAPACITY;
                    }
                    continue;
                }
                if (subtract_convex(aux, &naux, &cur[i], hp, hn) != 0) {
                    free(cells); free(next);
                    return CLIP_ERR_CAPACITY;
                }
            }
            Cell *swap = cur; cur = aux; aux = swap;
            ncur = naux;
        } else {
            CPoint tris[64][3];
            int nt = ear_clip(hp, hn, tris);
            if (nt < 0) {
                if (bad_hole) *bad_hole = h;
                free(cells); free(next);
                return CLIP_ERR_INTERNAL;
            }
            for (int t = 0; t < nt && ncur > 0; ++t) {
                int naux = 0;
                double tminx = tris[t][0].x, tminy = tris[t][0].y;
                double tmaxx = tris[t][0].x, tmaxy = tris[t][0].y;
                for (int z = 1; z < 3; ++z) {
                    if (tris[t][z].x < tminx) tminx = tris[t][z].x;
                    if (tris[t][z].x > tmaxx) tmaxx = tris[t][z].x;
                    if (tris[t][z].y < tminy) tminy = tris[t][z].y;
                    if (tris[t][z].y > tmaxy) tmaxy = tris[t][z].y;
                }
                for (int i = 0; i < ncur; ++i) {
                    double cminx, cminy, cmaxx, cmaxy;
                    if (cell_bbox(&cur[i], &cminx, &cminy, &cmaxx, &cmaxy) &&
                        !bbox_overlap(cminx, cminy, cmaxx, cmaxy,
                                      tminx, tminy, tmaxx, tmaxy)) {
                        if (add_cell(aux, &naux, cur[i].pts, cur[i].count) != 0) {
                            free(cells); free(next);
                            return CLIP_ERR_CAPACITY;
                        }
                        continue;
                    }
                    if (subtract_convex(aux, &naux, &cur[i], tris[t], 3) != 0) {
                        free(cells); free(next);
                        return CLIP_ERR_CAPACITY;
                    }
                }
                Cell *swap = cur; cur = aux; aux = swap;
                ncur = naux;
            }
        }
    }

    if (ncur > 0) {
        long live = 0;
        Seg *segs = build_boundary(cur, ncur, &live);
        free(cells);
        free(next);
        if (!segs) {
            if (live == 0 && ncur > 0) return CLIP_ERR_CAPACITY;
            return CLIP_ERR_CAPACITY;
        }
        int rc = assemble_rings(segs, live, out);
        free(segs);
        return rc;
    }

    free(cells);
    free(next);
    return CLIP_OK;
}

double clip_rings_area(const CPolyRings *r) {
    if (!r) return 0.0;
    double a = 0.0;
    for (int i = 0; i < r->outer_count; ++i)
        a += ring_area_raw(r->outer[i].pts, r->outer[i].count);
    for (int i = 0; i < r->hole_count; ++i)
        a += ring_area_raw(r->hole[i].pts, r->hole[i].count);
    return a;
}
