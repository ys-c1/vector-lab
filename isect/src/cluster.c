#include "cluster.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------- geometry */

static double dist2(IPoint a, IPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/* Distance comparisons carry a relative slack of a few ulps.  A radius is
 * searched over one build of a grid and then handed to a fresh clustering
 * pass; without the slack a pair at exactly the chosen radius can fall out
 * through rounding in r*r and silently become a missed merge.  The slack is
 * far below any tolerance a caller cares about. */
#define REL_SLACK 1e-12

static int within(double d2, double limit2) {
    return d2 <= limit2 * (1.0 + REL_SLACK);
}

static int clamp_count(long long v) {
    return v > 2147483647LL ? 2147483647 : (int)v;
}

/* ------------------------------------------------------------------- hits */

static int hit_push(IHitList *out, int *cap, IPoint p) {
    if (out->count == *cap) {
        int ncap = *cap ? *cap * 2 : 16;
        IPoint *q = (IPoint*)realloc(out->pts, sizeof(IPoint) * (size_t)ncap);
        if (!q) return ISECT_ENOMEM;
        out->pts = q;
        *cap = ncap;
    }
    out->pts[out->count++] = p;
    return 0;
}

int isect_polyline_hits(const IPoint *a, int na, const IPoint *b, int nb, IHitList *out) {
    if (!out) return ISECT_EINVAL;
    out->count = 0;
    out->pts = NULL;
    if (!a || !b || na < 2 || nb < 2) return ISECT_EINVAL;
    int cap = 0;
    for (int i = 0; i + 1 < na; ++i)
        for (int j = 0; j + 1 < nb; ++j) {
            IHit h;
            if (!isect_seg(a[i], a[i+1], b[j], b[j+1], &h)) continue;
            int rc = hit_push(out, &cap, h.at);
            if (rc != 0) { isect_hits_free(out); return rc; }
        }
    return 0;
}

int isect_hits_self(const IPoint *p, int n, IHitList *out) {
    if (!out) return ISECT_EINVAL;
    out->count = 0;
    out->pts = NULL;
    if (!p || n < 4) return ISECT_EINVAL;
    int cap = 0;
    for (int i = 0; i + 1 < n; ++i)
        for (int j = i + 2; j + 1 < n; ++j) {
            if (i == 0 && j == n - 2) continue;
            IHit h;
            if (!isect_seg(p[i], p[i+1], p[j], p[j+1], &h)) continue;
            int rc = hit_push(out, &cap, h.at);
            if (rc != 0) { isect_hits_free(out); return rc; }
        }
    return 0;
}

void isect_hits_free(IHitList *h) {
    if (!h) return;
    free(h->pts);
    h->pts = NULL;
    h->count = 0;
}

int isect_hits_write(FILE *f, const IHitList *h) {
    if (!f || !h) return ISECT_EINVAL;
    if (fprintf(f, "%d\n", h->count) < 0) return ISECT_EIO;
    for (int i = 0; i < h->count; ++i)
        if (fprintf(f, "%.17g %.17g\n", h->pts[i].x, h->pts[i].y) < 0) return ISECT_EIO;
    return 0;
}

int isect_hits_save(const char *path, const IHitList *h) {
    if (!path || !h) return ISECT_EINVAL;
    FILE *f = fopen(path, "w");
    if (!f) return ISECT_EIO;
    int rc = isect_hits_write(f, h);
    if (fclose(f) != 0 && rc == 0) rc = ISECT_EIO;
    return rc;
}

/* ------------------------------------------------------------- hit groups */

/* Identical hits are folded before any radius work.  A crossing can be
 * reported thousands of times at the very same coordinate; collapsing those
 * first makes every later step linear in the number of *distinct* positions
 * rather than quadratic in the number of reports, and it loses nothing: hits
 * at distance 0 belong to one cluster for every radius >= 0.
 *
 * Groups are numbered by coordinate order, and each keeps the smallest raw
 * hit index so cluster numbering still follows the first report. */
typedef struct {
    int m;        /* distinct coordinates */
    IPoint *rep;  /* representative coordinate of each group */
    int *w;       /* raw hits in the group */
    int *gid;     /* raw hit index -> group */
} Groups;

typedef struct { IPoint p; int idx; } PIdx;

static int cmp_pidx(const void *a, const void *b) {
    const PIdx *x = (const PIdx*)a, *y = (const PIdx*)b;
    if (x->p.x < y->p.x) return -1;
    if (x->p.x > y->p.x) return 1;
    if (x->p.y < y->p.y) return -1;
    if (x->p.y > y->p.y) return 1;
    return 0;
}

static void groups_free(Groups *g) {
    free(g->rep); free(g->w); free(g->gid);
    memset(g, 0, sizeof *g);
}

static int groups_build(const IPoint *pts, int n, Groups *g) {
    memset(g, 0, sizeof *g);
    if (n <= 0) return 0;
    PIdx *v = (PIdx*)malloc(sizeof(PIdx) * (size_t)n);
    g->gid = (int*)malloc(sizeof(int) * (size_t)n);
    g->rep = (IPoint*)malloc(sizeof(IPoint) * (size_t)n);
    g->w = (int*)malloc(sizeof(int) * (size_t)n);
    if (!v || !g->gid || !g->rep || !g->w) {
        free(v);
        groups_free(g);
        return ISECT_ENOMEM;
    }
    for (int i = 0; i < n; ++i) { v[i].p = pts[i]; v[i].idx = i; }
    qsort(v, (size_t)n, sizeof(PIdx), cmp_pidx);
    int m = 0, a = 0;
    while (a < n) {
        int b = a + 1;
        while (b < n && v[b].p.x == v[a].p.x && v[b].p.y == v[a].p.y) ++b;
        g->rep[m] = v[a].p;
        g->w[m] = b - a;
        for (int k = a; k < b; ++k) g->gid[v[k].idx] = m;
        ++m;
        a = b;
    }
    free(v);
    g->m = m;
    return 0;
}

/* -------------------------------------------------------------- cell grid */

/* Hash grid over group representatives.  Cells are at least as wide as any
 * radius that will be queried, so a point only ever looks at its 3x3
 * neighbourhood.  One build can serve a whole radius search. */
typedef struct {
    long long *cx, *cy;   /* cell of each group */
    long long *kx, *ky;   /* key stored in each occupied slot */
    int *head;            /* slot -> first group index, -1 when empty */
    int *next;            /* group index -> next group in the same cell */
    size_t mask;
} Grid;

static unsigned long long mix64(unsigned long long x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

static size_t grid_slot(const Grid *g, long long kx, long long ky) {
    unsigned long long h = mix64((unsigned long long)kx * 0x9e3779b97f4a7c15ULL ^
                                 (unsigned long long)ky);
    size_t s = (size_t)h & g->mask;
    while (g->head[s] != -1 && !(g->kx[s] == kx && g->ky[s] == ky))
        s = (s + 1) & g->mask;
    return s;
}

static void grid_free(Grid *g) {
    free(g->cx); free(g->cy); free(g->kx); free(g->ky);
    free(g->head); free(g->next);
    memset(g, 0, sizeof *g);
}

/* `cell` must be >= every radius later passed to grid_each_pair. */
static int grid_build(const IPoint *pts, int n, double cell, Grid *g) {
    memset(g, 0, sizeof *g);
    cell *= 1.0 + 1e-9;
    double minx = pts[0].x, miny = pts[0].y;
    for (int i = 1; i < n; ++i) {
        if (pts[i].x < minx) minx = pts[i].x;
        if (pts[i].y < miny) miny = pts[i].y;
    }
    size_t want = 16;
    while (want < (size_t)n * 2) want <<= 1;
    g->cx = (long long*)malloc(sizeof(long long) * (size_t)n);
    g->cy = (long long*)malloc(sizeof(long long) * (size_t)n);
    g->kx = (long long*)malloc(sizeof(long long) * want);
    g->ky = (long long*)malloc(sizeof(long long) * want);
    g->head = (int*)malloc(sizeof(int) * want);
    g->next = (int*)malloc(sizeof(int) * (size_t)n);
    if (!g->cx || !g->cy || !g->kx || !g->ky || !g->head || !g->next) {
        grid_free(g);
        return ISECT_ENOMEM;
    }
    g->mask = want - 1;
    for (size_t s = 0; s < want; ++s) g->head[s] = -1;
    for (int i = 0; i < n; ++i) {
        double fx = (pts[i].x - minx) / cell;
        double fy = (pts[i].y - miny) / cell;
        if (!(fx >= 0.0) || !(fy >= 0.0) || fx > 1e15 || fy > 1e15) {
            grid_free(g);
            return ISECT_EINVAL;
        }
        g->cx[i] = (long long)fx;
        g->cy[i] = (long long)fy;
        size_t s = grid_slot(g, g->cx[i], g->cy[i]);
        g->kx[s] = g->cx[i];
        g->ky[s] = g->cy[i];
        g->next[i] = g->head[s];
        g->head[s] = i;
    }
    return 0;
}

/* Return non-zero to stop the scan early (used while counting an over-budget
 * candidate radius, where the exact split count does not matter). */
typedef int (*PairFn)(void *ctx, int i, int j);

/* Call `fn` once for every pair (i < j) at distance <= radius, radius <= cell. */
static void grid_each_pair(const Grid *g, const IPoint *pts, int n, double radius,
                           PairFn fn, void *ctx) {
    double r2 = radius * radius;
    for (int i = 0; i < n; ++i)
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                size_t s = grid_slot(g, g->cx[i] + dx, g->cy[i] + dy);
                if (g->head[s] == -1) continue;
                for (int j = g->head[s]; j != -1; j = g->next[j]) {
                    if (j <= i) continue;
                    if (within(dist2(pts[i], pts[j]), r2) && fn(ctx, i, j)) return;
                }
            }
}

/* --------------------------------------------------------------- clusters */

void isect_clusters_free(IClusterSet *s) {
    if (!s) return;
    free(s->items);
    free(s->labels);
    s->items = NULL;
    s->labels = NULL;
    s->count = 0;
    s->n = 0;
    s->err_cluster = -1;
}

/* Union-find over groups.  Linking always points at the smaller root, so a
 * cluster is numbered by its smallest coordinate group and numbering is
 * stable from run to run. */
static int find(int *parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

static void unite(int *parent, int a, int b) {
    int ra = find(parent, a), rb = find(parent, b);
    if (ra == rb) return;
    if (ra < rb) parent[rb] = ra;
    else parent[ra] = rb;
}

static int unite_cb(void *ctx, int i, int j) {
    unite((int*)ctx, i, j);
    return 0;
}

static int reset_parent(int *parent, int m) {
    for (int i = 0; i < m; ++i) parent[i] = i;
    return 0;
}

static int link_radius(int *parent, const Grid *g, const IPoint *rep, int m,
                       double radius) {
    if (!(radius > 0.0) || m < 2) return 0;
    grid_each_pair(g, rep, m, radius, unite_cb, parent);
    return 0;
}

/* Weighted count of duplicate pairs split across components: each pair of
 * groups at distance <= tol contributes w[i]*w[j] if the groups are apart. */
typedef struct {
    int *parent;        /* union-find is read-only here but find() compresses */
    const int *w;
    long long split;
    long long limit;    /* stop once split > limit */
} SplitCtx;

static int split_cb(void *ctx, int i, int j) {
    SplitCtx *s = (SplitCtx*)ctx;
    if (find(s->parent, i) != find(s->parent, j)) {
        s->split += (long long)s->w[i] * (long long)s->w[j];
        if (s->split > s->limit) return 1;
    }
    return 0;
}

/* Duplicate pairs at `tol` split across components.  `limit` bounds the work
 * when the caller only needs to know the budget is exceeded. */
static int split_count(int *parent, const Grid *g, const IPoint *rep, int m,
                       const int *w, double tol, long long limit) {
    if (!(tol > 0.0) || m < 2) return 0;
    SplitCtx ctx = { parent, w, 0, limit };
    grid_each_pair(g, rep, m, tol, split_cb, &ctx);
    if (ctx.split > limit) return clamp_count(limit + 1);
    return clamp_count(ctx.split);
}

/* Centroid of each cluster over the original hits, and its worst deviation.
 * Weighted so a group costs one add per cluster no matter how often its
 * coordinate was reported. */
static int build_centroids(const Groups *g, const int *glabels, int k, int n,
                           IClusterSet *out) {
    out->items = (ICluster*)calloc((size_t)(k ? k : 1), sizeof(ICluster));
    if (!out->items) return ISECT_ENOMEM;
    out->count = k;
    out->n = n;
    for (int c = 0; c < k; ++c) out->items[c].first_index = -1;
    for (int gi = 0; gi < g->m; ++gi) {
        int c = glabels[gi];
        ICluster *cl = &out->items[c];
        cl->center.x += g->rep[gi].x * (double)g->w[gi];
        cl->center.y += g->rep[gi].y * (double)g->w[gi];
        cl->count += g->w[gi];
    }
    for (int c = 0; c < k; ++c) {
        ICluster *cl = &out->items[c];
        cl->center.x /= cl->count;
        cl->center.y /= cl->count;
    }
    for (int gi = 0; gi < g->m; ++gi) {
        int c = glabels[gi];
        double d = sqrt(dist2(g->rep[gi], out->items[c].center));
        if (d > out->items[c].max_dev) out->items[c].max_dev = d;
    }
    return 0;
}

/* Root labels -> dense 0..k-1, carrying each cluster's smallest group. */
static int label_components(int *parent, int m, int *labels, int *first_group,
                            int *nclusters) {
    if (m < 0) return ISECT_EINVAL;
    int *root_to_label = (int*)malloc(sizeof(int) * (size_t)(m ? m : 1));
    if (!root_to_label) return ISECT_ENOMEM;
    for (int i = 0; i < m; ++i) root_to_label[i] = -1;
    int k = 0;
    for (int i = 0; i < m; ++i) {
        int r = find(parent, i);
        if (root_to_label[r] < 0) {
            root_to_label[r] = k;
            if (first_group) first_group[k] = i;
            ++k;
        }
        labels[i] = root_to_label[r];
    }
    free(root_to_label);
    *nclusters = k;
    return 0;
}

/* Expand per-group labels to per-hit labels; set first_index from raw order. */
static int expand_labels(const Groups *g, const int *glabels, int n, int k,
                         IClusterSet *out) {
    out->labels = (int*)malloc(sizeof(int) * (size_t)(n ? n : 1));
    if (!out->labels) return ISECT_ENOMEM;
    for (int i = 0; i < n; ++i) out->labels[i] = glabels[g->gid[i]];
    for (int c = 0; c < k; ++c) out->items[c].first_index = -1;
    for (int i = 0; i < n; ++i) {
        int c = out->labels[i];
        if (out->items[c].first_index < 0) out->items[c].first_index = i;
    }
    return 0;
}

int isect_cluster_by_labels(const IPoint *pts, int n, const int *labels, int k,
                            IClusterSet *out) {
    if (!out) return ISECT_EINVAL;
    isect_clusters_free(out);
    if (!pts || !labels || n < 0 || k < 0) return ISECT_EINVAL;
    if (n == 0 && k == 0) return 0;
    if (n > 0 && k == 0) {
        out->err_cluster = 0;
        return ISECT_ECLUSTER;
    }
    for (int i = 0; i < n; ++i) {
        if (labels[i] < 0 || labels[i] >= k) {
            out->err_cluster = labels[i];
            return ISECT_ECLUSTER;
        }
    }
    /* An empty cluster is a caller bug worth naming explicitly: it usually
     * means an off-by-one in the cluster count or a lost group. */
    int *seen = (int*)calloc((size_t)(k ? k : 1), sizeof(int));
    if (!seen) return ISECT_ENOMEM;
    for (int i = 0; i < n; ++i) seen[labels[i]] = 1;
    for (int c = 0; c < k; ++c) {
        if (!seen[c]) {
            out->err_cluster = c;
            free(seen);
            return ISECT_ECLUSTER;
        }
    }
    free(seen);

    out->items = (ICluster*)calloc((size_t)k, sizeof(ICluster));
    out->labels = (int*)malloc(sizeof(int) * (size_t)(n ? n : 1));
    if (!out->items || !out->labels) { isect_clusters_free(out); return ISECT_ENOMEM; }
    for (int c = 0; c < k; ++c) out->items[c].first_index = -1;
    for (int i = 0; i < n; ++i) {
        int c = labels[i];
        out->items[c].center.x += pts[i].x;
        out->items[c].center.y += pts[i].y;
        out->items[c].count += 1;
        if (out->items[c].first_index < 0) out->items[c].first_index = i;
        out->labels[i] = c;
    }
    for (int c = 0; c < k; ++c) {
        out->items[c].center.x /= out->items[c].count;
        out->items[c].center.y /= out->items[c].count;
    }
    for (int i = 0; i < n; ++i) {
        int c = labels[i];
        double d = sqrt(dist2(pts[i], out->items[c].center));
        if (d > out->items[c].max_dev) out->items[c].max_dev = d;
    }
    out->count = k;
    out->n = n;
    return 0;
}

/* Cluster groups at `radius`: link, label, centroid, expand. */
static int cluster_groups(const Groups *g, int n, double radius, IClusterSet *out) {
    int *parent = (int*)malloc(sizeof(int) * (size_t)(g->m ? g->m : 1));
    int *glabels = (int*)malloc(sizeof(int) * (size_t)(g->m ? g->m : 1));
    if (!parent || !glabels) { free(parent); free(glabels); return ISECT_ENOMEM; }
    for (int i = 0; i < g->m; ++i) parent[i] = i;
    int rc = 0;
    if (radius > 0.0 && g->m >= 2) {
        Grid grid;
        rc = grid_build(g->rep, g->m, radius, &grid);
        if (rc == 0) {
            link_radius(parent, &grid, g->rep, g->m, radius);
            grid_free(&grid);
        }
    }
    int k = 0;
    if (rc == 0) rc = label_components(parent, g->m, glabels, NULL, &k);
    if (rc == 0) rc = build_centroids(g, glabels, k, n, out);
    if (rc == 0) rc = expand_labels(g, glabels, n, k, out);
    if (rc != 0) isect_clusters_free(out);
    free(parent);
    free(glabels);
    return rc;
}

int isect_cluster_by_radius(const IPoint *pts, int n, double radius, IClusterSet *out) {
    if (!out) return ISECT_EINVAL;
    isect_clusters_free(out);
    if (!pts || n < 0) return ISECT_EINVAL;
    if (!(radius >= 0.0) || isinf(radius)) return ISECT_ERADIUS;   /* also NaN */
    if (n == 0) return 0;
    Groups g;
    int rc = groups_build(pts, n, &g);
    if (rc == 0) rc = cluster_groups(&g, n, radius, out);
    groups_free(&g);
    return rc;
}

int isect_cluster_centers(const IClusterSet *cs, IHitList *out) {
    if (!out) return ISECT_EINVAL;
    out->count = 0;
    out->pts = NULL;
    if (!cs || cs->count < 0) return ISECT_EINVAL;
    if (cs->count == 0) return 0;
    out->pts = (IPoint*)malloc(sizeof(IPoint) * (size_t)cs->count);
    if (!out->pts) return ISECT_ENOMEM;
    for (int c = 0; c < cs->count; ++c) out->pts[c] = cs->items[c].center;
    out->count = cs->count;
    return 0;
}

double isect_merge_tol(void) { return ISECT_MERGE_TOL; }

int isect_cluster_max_dev(const IClusterSet *cs, double *max_dev, int *cluster) {
    if (!cs || !max_dev) return ISECT_EINVAL;
    *max_dev = 0.0;
    if (cluster) *cluster = -1;
    for (int c = 0; c < cs->count; ++c) {
        if (cs->items[c].max_dev > *max_dev) {
            *max_dev = cs->items[c].max_dev;
            if (cluster) *cluster = c;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ miss budget */

/* Split count from per-hit labels.  Groups are uniform for every set this
 * module produces; a group whose reports carry different labels has its
 * in-group cross-label pairs counted exactly and its cross-group pairs
 * weighted by the first label. */
static int split_for_labels(const Groups *g, const int *hit_labels, const IPoint *pts,
                            int n, double tol) {
    long long split = 0;
    int *glab = (int*)malloc(sizeof(int) * (size_t)(g->m ? g->m : 1));
    int *mixed = (int*)calloc((size_t)(g->m ? g->m : 1), sizeof(int));
    int *first = (int*)malloc(sizeof(int) * (size_t)(g->m ? g->m : 1));
    if (!glab || !mixed || !first) { free(glab); free(mixed); free(first); return ISECT_EINVAL; }
    (void)pts; (void)n;
    for (int i = 0; i < g->m; ++i) first[i] = -1;
    for (int i = 0; i < n; ++i) {
        int gi = g->gid[i];
        if (first[gi] < 0) first[gi] = hit_labels[i];
        else if (hit_labels[i] != first[gi]) mixed[gi] = 1;
        glab[gi] = first[gi];
    }
    /* in-group pairs with different labels are at distance 0 and always split */
    for (int gi = 0; gi < g->m; ++gi)
        if (mixed[gi] && g->w[gi] > 1) {
            int *buf = (int*)malloc(sizeof(int) * (size_t)g->w[gi]);
            if (!buf) { free(glab); free(mixed); free(first); return ISECT_EINVAL; }
            int t = 0;
            for (int i = 0; i < n; ++i)
                if (g->gid[i] == gi) buf[t++] = hit_labels[i];
            for (int a = 1; a < t; ++a) {   /* insertion sort: tiny label run */
                int v = buf[a], b = a - 1;
                while (b >= 0 && buf[b] > v) { buf[b+1] = buf[b]; --b; }
                buf[b+1] = v;
            }
            long long total = (long long)t * (t - 1) / 2, same = 0;
            for (int a = 0; a < t; ) {
                int b = a + 1;
                while (b < t && buf[b] == buf[a]) ++b;
                long long c = b - a;
                same += c * (c - 1) / 2;
                a = b;
            }
            split += total - same;
            free(buf);
        }
    if (tol > 0.0 && g->m >= 2) {
        Grid grid;
        if (grid_build(g->rep, g->m, tol, &grid) != 0) {
            free(glab); free(mixed); free(first);
            return ISECT_EINVAL;
        }
        for (int i = 0; i < g->m; ++i)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    size_t s = grid_slot(&grid, grid.cx[i] + dx, grid.cy[i] + dy);
                    if (grid.head[s] == -1) continue;
                    for (int j = grid.head[s]; j != -1; j = grid.next[j]) {
                        if (j <= i) continue;
                        if (!within(dist2(g->rep[i], g->rep[j]), tol * tol)) continue;
                        if (glab[i] != glab[j])
                            split += (long long)g->w[i] * (long long)g->w[j];
                    }
                }
        grid_free(&grid);
    }
    free(glab);
    free(mixed);
    free(first);
    return clamp_count(split);
}

int isect_missed_merges(const IPoint *pts, int n, const IClusterSet *cs,
                        double merge_tol, int *missed) {
    if (!missed) return ISECT_EINVAL;
    *missed = 0;
    if (!pts || !cs || !cs->labels || cs->n != n) return ISECT_EINVAL;
    if (!(merge_tol >= 0.0) || isinf(merge_tol)) return ISECT_EINVAL;
    if (n < 2) return 0;
    Groups g;
    int rc = groups_build(pts, n, &g);
    if (rc != 0) return rc;
    *missed = split_for_labels(&g, cs->labels, pts, n, merge_tol);
    groups_free(&g);
    return 0;
}

/* A bounded list of candidate pairs.  Collecting them once and bisecting on
 * the sorted distances costs O(pairs) per probe instead of a full grid scan. */
typedef struct { int i, j; double d; } HitPair;

typedef struct {
    HitPair *v;
    int count, cap, overflow;
} PairSink;

static int pair_cmp(const void *a, const void *b) {
    double x = ((const HitPair*)a)->d, y = ((const HitPair*)b)->d;
    return (x > y) - (x < y);
}

/* Enough for tens of thousands of hits; a runaway case falls back to the
 * grid instead of exhausting memory. */
#define PAIR_CAP (1 << 21)

static int collect_cb(void *ctx, int i, int j) {
    PairSink *s = (PairSink*)ctx;
    if (s->count == s->cap) {
        if (s->cap >= PAIR_CAP) { s->overflow = 1; return 1; }
        int ncap = s->cap ? s->cap * 2 : 1024;
        if (ncap > PAIR_CAP) ncap = PAIR_CAP;
        HitPair *q = (HitPair*)realloc(s->v, sizeof(HitPair) * (size_t)ncap);
        if (!q) { s->overflow = 1; return 1; }
        s->v = q;
        s->cap = ncap;
    }
    s->v[s->count].i = i;
    s->v[s->count].j = j;
    s->v[s->count].d = 0.0;   /* filled below, the sink has no coordinates */
    s->count++;
    return 0;
}

/* Link every pair with distance <= r, growing the partition incrementally.
 * Bisection probes monotonically outward from the previous radius, so the
 * pairs already united are exactly the prefix below the last probe. */
static void link_prefix(int *parent, const HitPair *v, int from, int to) {
    for (int p = from; p < to; ++p) unite(parent, v[p].i, v[p].j);
}

/* Weighted split count for the given components (same convention as
 * split_count above). */
static int split_prefix(int *parent, const HitPair *v, int n, const int *w,
                        double tol, long long limit) {
    long long split = 0;
    for (int p = 0; p < n; ++p) {
        if (!(v[p].d <= tol)) break;
        if (find(parent, v[p].i) != find(parent, v[p].j)) {
            split += (long long)w[v[p].i] * (long long)w[v[p].j];
            if (split > limit) return clamp_count(limit + 1);
        }
    }
    return clamp_count(split);
}

/* Infimum radius in [0, merge_tol] meeting the miss budget.  split(r) is
 * non-increasing in r, so bisection over the sorted pair distances yields the
 * smallest sufficient radius.  Radius 0 means "identical hits only" and is
 * already folded into groups, so the split count there is every distinct pair
 * within the tolerance. */
static int radius_for_groups(const Groups *g, double merge_tol, int max_missed,
                             double *radius, int *fell_back) {
    *radius = 0.0;
    *fell_back = 0;
    if (g->m < 2 || merge_tol == 0.0) return 0;
    int *parent = (int*)malloc(sizeof(int) * (size_t)g->m);
    if (!parent) return ISECT_ENOMEM;

    Grid grid;
    int rc = grid_build(g->rep, g->m, merge_tol, &grid);
    if (rc != 0) { free(parent); return rc; }

    PairSink sink;
    memset(&sink, 0, sizeof sink);
    grid_each_pair(&grid, g->rep, g->m, merge_tol, collect_cb, &sink);
    if (sink.overflow) {
        /* Dense input: fall back to the grid-driven bisection below. */
        free(sink.v);
        *fell_back = 1;
        grid_free(&grid);
        free(parent);
        return ISECT_EINVAL;
    }
    for (int p = 0; p < sink.count; ++p)
        sink.v[p].d = sqrt(dist2(g->rep[sink.v[p].i], g->rep[sink.v[p].j]));
    qsort(sink.v, (size_t)sink.count, sizeof(HitPair), pair_cmp);

    reset_parent(parent, g->m);
    long long limit = max_missed;
    int split0 = split_prefix(parent, sink.v, sink.count, g->w, merge_tol, limit);
    if (split0 <= max_missed) {
        free(sink.v);
        grid_free(&grid);
        free(parent);
        return 0;
    }
    /* Search the sorted pair array: components at any radius are a prefix of
     * it, so bisecting on prefix length converges in log2(pairs) steps.  The
     * smallest sufficient prefix's largest distance is the answer.  Equal
     * distances at the boundary need no special handling -- a radius cannot
     * include one without the others, so the returned radius covers them all. */
    int lo = 0, hi = sink.count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        reset_parent(parent, g->m);
        link_prefix(parent, sink.v, 0, mid);
        if (split_prefix(parent, sink.v, sink.count, g->w, merge_tol, limit) <= max_missed)
            hi = mid;
        else
            lo = mid + 1;
    }
    *radius = lo > 0 ? sink.v[lo - 1].d : 0.0;
    free(sink.v);
    grid_free(&grid);
    free(parent);
    return 0;
}

/* Grid-only bisection, used when the pair list would be too large.  Counting
 * exits as soon as the budget is exceeded, so over-budget probes are cheap. */
static int radius_for_groups_grid(const Groups *g, double merge_tol, int max_missed,
                                  double *radius) {
    *radius = 0.0;
    int *parent = (int*)malloc(sizeof(int) * (size_t)g->m);
    if (!parent) return ISECT_ENOMEM;
    Grid grid;
    int rc = grid_build(g->rep, g->m, merge_tol, &grid);
    if (rc != 0) { free(parent); return rc; }
    long long limit = max_missed;
    reset_parent(parent, g->m);
    if (split_count(parent, &grid, g->rep, g->m, g->w, merge_tol, limit) <= max_missed) {
        grid_free(&grid);
        free(parent);
        return 0;
    }
    double lo = 0.0, hi = merge_tol;
    for (int it = 0; it < 64; ++it) {
        double mid = lo + (hi - lo) * 0.5;
        if (mid == lo || mid == hi) break;
        reset_parent(parent, g->m);
        link_radius(parent, &grid, g->rep, g->m, mid);
        if (split_count(parent, &grid, g->rep, g->m, g->w, merge_tol, limit) <= max_missed)
            hi = mid;
        else
            lo = mid;
    }
    *radius = hi;
    grid_free(&grid);
    free(parent);
    return 0;
}

int isect_radius_for_missed(const IPoint *pts, int n, double merge_tol,
                            int max_missed, double *radius) {
    if (!radius) return ISECT_EINVAL;
    *radius = 0.0;
    if (!pts || n < 0 || max_missed < 0) return ISECT_EINVAL;
    if (!(merge_tol >= 0.0) || isinf(merge_tol)) return ISECT_EINVAL;
    if (n < 2) return 0;
    Groups g;
    int rc = groups_build(pts, n, &g);
    if (rc != 0) return rc;
    int fell_back = 0;
    rc = radius_for_groups(&g, merge_tol, max_missed, radius, &fell_back);
    if (rc == ISECT_EINVAL && fell_back)
        rc = radius_for_groups_grid(&g, merge_tol, max_missed, radius);
    groups_free(&g);
    return rc;
}

/* ----------------------------------------------------------------- driver */

int isect_resolve_hits(const IHitList *raw, int mode, double merge_tol, int max_missed,
                       IClusterSet *clusters, double *radius, int *missed) {
    if (!raw) return ISECT_EINVAL;
    if (mode == ISECT_MODE_RAW) {
        if (radius) *radius = 0.0;
        if (missed) *missed = 0;
        return 0;
    }
    if (mode != ISECT_MODE_CLUSTER) return ISECT_EINVAL;
    if (!clusters) return ISECT_EINVAL;
    isect_clusters_free(clusters);
    if (radius) *radius = 0.0;
    if (missed) *missed = 0;
    if (!(merge_tol >= 0.0) || isinf(merge_tol) || max_missed < 0) return ISECT_EINVAL;
    if (raw->count == 0) return 0;

    Groups g;
    int rc = groups_build(raw->pts, raw->count, &g);
    if (rc != 0) return rc;
    double r = 0.0;
    int fell_back = 0;
    rc = radius_for_groups(&g, merge_tol, max_missed, &r, &fell_back);
    if (rc == ISECT_EINVAL && fell_back)
        rc = radius_for_groups_grid(&g, merge_tol, max_missed, &r);
    if (rc == 0) rc = cluster_groups(&g, raw->count, r, clusters);
    int got = 0;
    if (rc == 0) {
        rc = isect_missed_merges(raw->pts, raw->count, clusters, merge_tol, &got);
        if (rc != 0) { isect_clusters_free(clusters); }
    }
    if (rc == 0 && got > max_missed) {
        isect_clusters_free(clusters);
        rc = ISECT_EMISSED;
    }
    if (rc == 0) {
        /* Position error is a hard ceiling, not a diagnostic: if single
         * linkage had to chain hits further than half a pixel apart to reach
         * the miss budget, no centroid represents the cluster and the request
         * is refused rather than answered with a misleading point. */
        double dev = 0.0;
        int bad = -1;
        isect_cluster_max_dev(clusters, &dev, &bad);
        if (dev > merge_tol) {
            isect_clusters_free(clusters);
            clusters->err_cluster = bad;
            rc = ISECT_EDEVIATION;
        }
    }
    if (rc == 0) {
        if (radius) *radius = r;
        if (missed) *missed = got;
    }
    groups_free(&g);
    return rc;
}

int isect_resolve(const IPoint *a, int na, const IPoint *b, int nb, int mode,
                  double merge_tol, int max_missed,
                  IHitList *raw, IClusterSet *clusters,
                  double *radius, int *missed) {
    if (!raw) return ISECT_EINVAL;
    int rc = isect_polyline_hits(a, na, b, nb, raw);
    if (rc != 0) return rc;
    return isect_resolve_hits(raw, mode, merge_tol, max_missed, clusters, radius, missed);
}
