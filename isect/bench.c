/* Benchmark: duplicate count, order error and timing for isect_polyline_list
 * on 64- and 1024-point polylines, plus a before/after dedup listing.
 *
 * Polyline A: n points on y=0 with jumping x: 0, n-1, 1, n-2, 2, ...
 * Polyline B: zigzag (j+0.5, +-1), crosses y=0 at every integer x = 1..n-1.
 * Every crossing at an A vertex is reported once per A segment covering it,
 * so the raw list is full of duplicates and (A's x jumps) out of order.
 * Unique intersections are exactly (1,0)..(n-1,0): n-1 points. */
#define _POSIX_C_SOURCE 199309L
#include "isect.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

typedef struct { IPoint p; int idx; } Ord;

static int cmp_ord(const void *pa, const void *pb) {
    const Ord *a = (const Ord *)pa, *b = (const Ord *)pb;
    if (a->p.x != b->p.x) return a->p.x < b->p.x ? -1 : 1;
    if (a->p.y != b->p.y) return a->p.y < b->p.y ? -1 : 1;
    return a->idx - b->idx;
}

static int same_pt(IPoint p, IPoint q) {
    double sx = 1.0 + fmax(fabs(p.x), fabs(q.x));
    double sy = 1.0 + fmax(fabs(p.y), fabs(q.y));
    return fabs(p.x - q.x) <= 1e-9 * sx && fabs(p.y - q.y) <= 1e-9 * sy;
}

static long long merge_count(int *v, int *tmp, int lo, int hi) {
    if (hi - lo < 2) return 0;
    int mid = lo + (hi - lo) / 2;
    long long inv = merge_count(v, tmp, lo, mid) + merge_count(v, tmp, mid, hi);
    int i = lo, j = mid, k = lo;
    while (i < mid && j < hi) {
        if (v[i] <= v[j]) tmp[k++] = v[i++];
        else { tmp[k++] = v[j++]; inv += mid - i; }
    }
    while (i < mid) tmp[k++] = v[i++];
    while (j < hi) tmp[k++] = v[j++];
    for (i = lo; i < hi; ++i) v[i] = tmp[i];
    return inv;
}

static int run_group(int n) {
    int bad = 0;
    IPoint *a = malloc((size_t)n * sizeof *a);
    IPoint *b = malloc((size_t)n * sizeof *b);
    for (int i = 0; i < n; ++i) {
        a[i].x = (i % 2 == 0) ? i / 2 : n - 1 - i / 2;
        a[i].y = 0.0;
        b[i].x = i + 0.5;
        b[i].y = (i % 2) ? 1.0 : -1.0;
    }
    int cap = (n - 1) * (n - 1);
    IXHit *raw = malloc((size_t)cap * sizeof *raw);
    IXHit *ded = malloc((size_t)cap * sizeof *ded);
    char err[256];

    /* analytic expectation: crossings at integer k=1..n-1; A segment i
     * covers k iff k in [min(x_i,x_{i+1}), max(x_i,x_{i+1})] */
    int expect_raw = 0;
    for (int i = 0; i + 1 < n; ++i) {
        int lo = (int)fmin(a[i].x, a[i+1].x), hi = (int)fmax(a[i].x, a[i+1].x);
        if (lo < 1) lo = 1;
        if (hi > n - 1) hi = n - 1;
        if (hi >= lo) expect_raw += hi - lo + 1;
    }

    int nraw = 0, nded = 0;
    double t0 = now_ms();
    int rc = isect_polyline_list(a, n, b, n, raw, cap, &nraw, 0, err, sizeof err);
    double t_collect = now_ms() - t0;
    if (rc != ISECT_OK) { printf("n=%d raw failed: %s\n", n, err); return 1; }

    t0 = now_ms();
    rc = isect_polyline_list(a, n, b, n, ded, cap, &nded, 1, err, sizeof err);
    double t_dedup = now_ms() - t0;
    if (rc != ISECT_OK) { printf("n=%d dedup failed: %s\n", n, err); return 1; }

    int hits2 = 0;
    isect_polyline(a, n, b, n, &hits2);

    /* order error: rank of each raw hit in the coordinate-sorted order */
    Ord *ord = malloc((size_t)nraw * sizeof *ord);
    int *rank = malloc((size_t)nraw * sizeof *rank);
    int *tmp = malloc((size_t)nraw * sizeof *tmp);
    for (int i = 0; i < nraw; ++i) { ord[i].p = raw[i].at; ord[i].idx = i; }
    qsort(ord, (size_t)nraw, sizeof *ord, cmp_ord);
    int nrank = 0;
    for (int i = 0; i < nraw; ++i) {
        if (i > 0 && !same_pt(ord[i].p, ord[i-1].p)) ++nrank;
        rank[ord[i].idx] = nrank;
    }
    ++nrank;
    int descents = 0;
    for (int i = 0; i + 1 < nraw; ++i)
        if (rank[i] > rank[i+1]) ++descents;
    /* first-occurrence order vs sorted order */
    int *seen = calloc((size_t)nrank, sizeof *seen);
    int misplaced = 0, pos = 0;
    for (int i = 0; i < nraw; ++i) {
        if (seen[rank[i]]) continue;
        seen[rank[i]] = 1;
        if (rank[i] != pos) ++misplaced;
        ++pos;
    }
    long long inversions = merge_count(rank, tmp, 0, nraw); /* sorts rank[] */

    /* verify deduped list == hand-derived table (k, 0), k = 1..n-1 */
    int tbl_bad = 0;
    if (nded != n - 1) tbl_bad = nded;
    else
        for (int k = 1; k <= n - 1; ++k)
            if (fabs(ded[k-1].at.x - k) > 1e-9 || fabs(ded[k-1].at.y) > 1e-9) tbl_bad = k;
    if (nraw != expect_raw || nraw != hits2 || tbl_bad) bad = 1;

    printf("=== n=%d ===\n", n);
    printf("raw hits            : %d (expected %d, isect_polyline %d)%s\n",
           nraw, expect_raw, hits2, (nraw == expect_raw && nraw == hits2) ? "" : "  MISMATCH");
    printf("unique after dedup  : %d (expected %d)%s\n",
           nded, n - 1, nded == n - 1 ? "" : "  MISMATCH");
    printf("duplicates removed  : %d\n", nraw - nded);
    printf("hand table check    : %s\n", tbl_bad ? "MISMATCH" : "ok (1,0)..(n-1,0)");
    printf("order error         : %lld inversions, %d descents, %d/%d first-occurrence misplaced\n",
           inversions, descents, misplaced, nded);
    printf("time                : collect %.3f ms, collect+sort+dedup %.3f ms\n",
           t_collect, t_dedup);

    /* before/after dedup differences */
    printf("dedup diff (removed duplicates):\n");
    int shown = 0, clusters = 0;
    for (int i = 0; i < nraw;) {
        int j = i + 1;
        while (j < nraw && same_pt(ord[j].p, ord[i].p)) ++j;
        if (j - i > 1) {
            ++clusters;
            if (n <= 64 || shown < 10) {
                printf("  (%g, %g) x%d -> 1  [segments ia=", ord[i].p.x, ord[i].p.y, j - i);
                for (int m = i; m < j; ++m) printf("%s%d", m > i ? "," : "", raw[ord[m].idx].ia);
                printf("]\n");
                ++shown;
            }
        }
        i = j;
    }
    if (clusters > shown) printf("  ... %d more duplicate clusters\n", clusters - shown);
    printf("duplicate clusters  : %d\n\n", clusters);

    free(a); free(b); free(raw); free(ded); free(ord); free(rank); free(tmp); free(seen);
    return bad;
}

int main(void) {
    int bad = 0;
    bad |= run_group(64);
    bad |= run_group(1024);
    return bad;
}
