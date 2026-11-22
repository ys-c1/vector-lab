#include "isect.h"
#include "cluster.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* ---------------------------------------------------------- shared fixtures */

/* Synthetic crossing reports: `contacts` distinct crossings, each reported
 * `per_contact` times with sub-pixel jitter, so a correct clusterer must fold
 * every group back to one point.  Crossings sit on a 4 px grid, far outside
 * the 0.5 px merge tolerance, so no two groups can ever merge. */
static IPoint *make_grouped(int contacts, int per_contact, double jitter, int *n_out) {
    int n = contacts * per_contact;
    IPoint *pts = (IPoint*)malloc(sizeof(IPoint) * (size_t)n);
    if (!pts) return NULL;
    for (int g = 0; g < contacts; ++g) {
        double bx = 4.0 * (double)(g % 64);
        double by = 4.0 * (double)(g / 64);
        for (int m = 0; m < per_contact; ++m) {
            unsigned k = (unsigned)(g * 1103515245u + m * 12345u + 101u);
            double jx = (double)((k >> 8) % 1001u) / 1000.0 * 2.0 - 1.0;
            double jy = (double)((k >> 19) % 1001u) / 1000.0 * 2.0 - 1.0;
            pts[g * per_contact + m].x = bx + jx * jitter;
            pts[g * per_contact + m].y = by + jy * jitter;
        }
    }
    *n_out = n;
    return pts;
}

static int same_bytes(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return 0; }
    int same = 1;
    unsigned char ba[4096], bb[4096];
    for (;;) {
        size_t na = fread(ba, 1, sizeof ba, fa);
        size_t nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na) != 0) { same = 0; break; }
        if (na == 0) break;
    }
    fclose(fa);
    fclose(fb);
    return same;
}

/* Independent label-free check: every pair of points closer than tol must
 * share a label.  Deliberately O(n^2) and written differently from the
 * library, so it can catch a wrong radius or a wrong union. */
static int missed_by_bruteforce(const IPoint *p, int n, const IClusterSet *cs, double tol) {
    int miss = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            double dx = p[i].x - p[j].x, dy = p[i].y - p[j].y;
            if (dx * dx + dy * dy <= tol * tol && cs->labels[i] != cs->labels[j]) ++miss;
        }
    return miss;
}

/* ------------------------------------------------------------- legacy tests */

static void test_legacy(void) {
    IPoint a0 = {0,0}, a1 = {10,0}, b0 = {5,-5}, b1 = {5,5};
    IHit h;
    CK(isect_seg(a0,a1,b0,b1,&h) == 1);
    CK(h.at.x > 4.9 && h.at.x < 5.1);
    IPoint c0 = {0,1}, c1 = {10,1};
    CK(isect_seg(a0,a1,c0,c1,&h) == 0);
    IPoint pa[2] = {{0,0},{10,0}}, pb[2] = {{5,-5},{5,5}};
    int hits = 0;
    CK(isect_polyline(pa,2,pb,2,&hits) == 0 && hits == 1);
    IPoint bow[5] = {{0,0},{10,10},{10,0},{0,10},{0,0}};
    int ia = -1, ib = -1;
    CK(isect_self(bow,5,&ia,&ib) == 1);
    CK(isect_polyline(pa,1,pb,2,&hits) == -1);
}

/* ----------------------------------------------- raw list: byte invariance */

static void test_raw_unchanged(void) {
    /* Same inputs the goldens were produced from, with the pre-change code. */
    IPoint a1[3] = {{0,0},{5,0},{10,0}};
    IPoint b1[9] = {{1,1},{2,0},{3,1},{4,1},{5,0},{6,1},{7,1},{8,0},{9,1}};
    IPoint a2[5] = {{0,0},{10,0},{10,10},{0,10},{0,0}};
    IPoint b2[6] = {{2,5},{5,-2},{5,0},{5,0},{5,12},{8,5}};

    IHitList raw;
    CK(isect_polyline_hits(a1, 3, b1, 9, &raw) == 0);
    CK(raw.count == 8);
    CK(isect_hits_save("build/raw_v.check.txt", &raw) == 0);
    isect_hits_free(&raw);
    CK(same_bytes("build/raw_v.check.txt", "golden/raw_v.txt"));

    /* Turning the new logic off must hand back exactly the legacy bytes. */
    double radius = 123.0;
    int missed = -1;
    CK(isect_resolve(a1, 3, b1, 9, ISECT_MODE_RAW, ISECT_MERGE_TOL, 0,
                     &raw, NULL, &radius, &missed) == 0);
    CK(raw.count == 8);
    CK(radius == 0.0 && missed == 0);
    CK(isect_hits_save("build/raw_v.off.txt", &raw) == 0);
    isect_hits_free(&raw);
    CK(same_bytes("build/raw_v.off.txt", "golden/raw_v.txt"));

    CK(isect_resolve(a2, 5, b2, 6, ISECT_MODE_RAW, ISECT_MERGE_TOL, 0,
                     &raw, NULL, NULL, NULL) == 0);
    CK(isect_hits_save("build/raw_square.off.txt", &raw) == 0);
    isect_hits_free(&raw);
    CK(same_bytes("build/raw_square.off.txt", "golden/raw_square.txt"));
}

/* ------------------------------------------------------- radius inversion */

/* Small brute-force oracle: the smallest candidate radius (0 or an actual
 * pair distance <= tol) that leaves at most `budget` close pairs split.  It
 * shares no code with the library, so an inversion bug shows up as a
 * mismatch rather than as agreement with itself. */
static double oracle_radius(const IPoint *p, int n, double tol, int budget) {
    double cand[128];
    int m = 0;
    cand[m++] = 0.0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            double dx = p[i].x - p[j].x, dy = p[i].y - p[j].y;
            double d = sqrt(dx*dx + dy*dy);
            if (d <= tol && m < 128) cand[m++] = d;
        }
    for (int a = 1; a < m; ++a) {          /* sort ascending */
        double v = cand[a];
        int b = a - 1;
        while (b >= 0 && cand[b] > v) { cand[b+1] = cand[b]; --b; }
        cand[b+1] = v;
    }
    for (int c = 0; c < m; ++c) {
        double r = cand[c];
        int par[16];
        for (int i = 0; i < n; ++i) par[i] = i;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                double dx = p[i].x - p[j].x, dy = p[i].y - p[j].y;
                if (sqrt(dx*dx + dy*dy) > r) continue;
                int a = i, b = j;
                while (par[a] != a) a = par[a];
                while (par[b] != b) b = par[b];
                if (a != b) par[a] = b;
            }
        int split = 0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) {
                double dx = p[i].x - p[j].x, dy = p[i].y - p[j].y;
                if (sqrt(dx*dx + dy*dy) > tol) continue;
                int a = i, b = j;
                while (par[a] != a) a = par[a];
                while (par[b] != b) b = par[b];
                if (a != b) ++split;
            }
        if (split <= budget) return r;
    }
    return -1.0;
}

static int cmp_d(const void *a, const void *b) {
    double x = *(const double*)a, y = *(const double*)b;
    return (x > y) - (x < y);
}

static void test_radius_inversion(void) {
    /* Chain 0-1-2-3 at 0.1/0.35/0.2 plus 4-5 at 0.3.  The smallest radius
     * that leaves zero duplicate pairs split is 0.35 (0.1/0.2/0.3 each leave
     * a pair across components); this is what inverting the budget must find,
     * not the largest pair distance 0.45. */
    IPoint p[6] = {
        {0.0, 0.0}, {0.1, 0.0}, {0.45, 0.0}, {0.65, 0.0},
        {5.0, 5.0}, {5.3, 5.0}
    };
    int n = 6;
    int budget0 = 0, budget1 = 1;
    double r0 = -1.0, r1 = -1.0;
    CK(isect_radius_for_missed(p, n, ISECT_MERGE_TOL, budget0, &r0) == 0);
    CK(isect_radius_for_missed(p, n, ISECT_MERGE_TOL, budget1, &r1) == 0);
    double o0 = oracle_radius(p, n, ISECT_MERGE_TOL, budget0);
    double o1 = oracle_radius(p, n, ISECT_MERGE_TOL, budget1);
    CK(fabs(r0 - o0) < 1e-12);
    CK(fabs(r1 - o1) < 1e-12);
    CK(r0 > 0.349 && r0 < 0.351);
    CK(r0 <= o0 + 1e-12 && r1 <= r0);

    /* Inverted radius actually delivers the budget. */
    IClusterSet cs = {0};
    CK(isect_cluster_by_radius(p, n, r0, &cs) == 0);
    int miss = -1;
    CK(isect_missed_merges(p, n, &cs, ISECT_MERGE_TOL, &miss) == 0);
    CK(miss == 0);
    isect_clusters_free(&cs);

    /* No duplicates at all: radius 0 and one cluster per point. */
    IPoint far[4] = {{0,0},{1,0},{0,1},{1,1}};
    double r = -1.0;
    CK(isect_radius_for_missed(far, 4, ISECT_MERGE_TOL, 0, &r) == 0);
    CK(r == 0.0);
    CK(isect_cluster_by_radius(far, 4, r, &cs) == 0);
    CK(cs.count == 4);
    isect_clusters_free(&cs);

    /* Duplicates exactly coincident: radius 0 still merges them. */
    IPoint dup[4] = {{2,2},{2,2},{2,2},{8,8}};
    CK(isect_radius_for_missed(dup, 4, ISECT_MERGE_TOL, 0, &r) == 0);
    CK(r == 0.0);
    CK(isect_cluster_by_radius(dup, 4, r, &cs) == 0);
    CK(cs.count == 2);
    isect_clusters_free(&cs);

    /* IDs that a per-run map would break: shuffled input order must still
     * produce one cluster for the coincident trio. */
    IPoint shuffled[4] = {{8,8},{2,2},{2,2},{2,2}};
    CK(isect_cluster_by_radius(shuffled, 4, 0.0, &cs) == 0);
    CK(cs.count == 2);
    CK(cs.labels[0] != cs.labels[1] && cs.labels[1] == cs.labels[2]);
    isect_clusters_free(&cs);

    /* Radius search must find the smallest sufficient radius for random
     * instances, not just a sufficient one.  Compare against the oracle. */
    unsigned seed = 12345u;
    int oracle_ok = 1;
    for (int t = 0; t < 40; ++t) {
        IPoint rp[12];
        for (int i = 0; i < 12; ++i) {
            seed = seed * 1664525u + 1013904223u;
            rp[i].x = (double)(seed % 1000u) / 1000.0;
            seed = seed * 1664525u + 1013904223u;
            rp[i].y = (double)(seed % 1000u) / 1000.0;
        }
        for (int b = 0; b <= 3; ++b) {
            double got = -1.0;
            int rc = isect_radius_for_missed(rp, 12, ISECT_MERGE_TOL, b, &got);
            if (rc != 0) { oracle_ok = 0; break; }
            double want = oracle_radius(rp, 12, ISECT_MERGE_TOL, b);
            if (fabs(got - want) > 1e-12) { oracle_ok = 0; break; }
        }
    }
    CK(oracle_ok);

    /* Negative budget is meaningless and must be rejected. */
    double bad = 0.0;
    CK(isect_radius_for_missed(p, n, ISECT_MERGE_TOL, -1, &bad) == ISECT_EINVAL);
    CK(isect_radius_for_missed(p, n, -1.0, 0, &bad) != 0);
}

/* --------------------------------------------------- rejection: bad radius */

static void test_reject_radius(void) {
    IPoint p[3] = {{0,0},{1,0},{2,0}};
    IClusterSet cs = {0};
    double nan_r = 0.0 / 0.0;
    double inf_r = 1.0 / 0.0;
    CK(isect_cluster_by_radius(p, 3, -0.25, &cs) == ISECT_ERADIUS);
    CK(isect_cluster_by_radius(p, 3, nan_r, &cs) == ISECT_ERADIUS);
    CK(isect_cluster_by_radius(p, 3, inf_r, &cs) == ISECT_ERADIUS);
    CK(cs.count == 0 && cs.labels == NULL);
    /* Zero and positive radii are accepted. */
    CK(isect_cluster_by_radius(p, 3, 0.0, &cs) == 0 && cs.count == 3);
    isect_clusters_free(&cs);
    CK(isect_cluster_by_radius(p, 3, 1.0, &cs) == 0 && cs.count == 1);
    isect_clusters_free(&cs);
}

/* ---------------------------------------------- rejection: empty clusters */

static void test_reject_empty_cluster(void) {
    IPoint p[4] = {{0,0},{0.1,0},{5,5},{9,9}};
    IClusterSet cs = {0};
    /* labels skip 1 -> cluster 1 is empty and must be named. */
    int labels_gap[4] = {0, 0, 2, 2};
    CK(isect_cluster_by_labels(p, 4, labels_gap, 3, &cs) == ISECT_ECLUSTER);
    CK(cs.err_cluster == 1);
    CK(cs.items == NULL && cs.labels == NULL && cs.count == 0);

    /* A label outside [0,k) names that label. */
    int labels_oob[4] = {0, 0, 1, 7};
    CK(isect_cluster_by_labels(p, 4, labels_oob, 2, &cs) == ISECT_ECLUSTER);
    CK(cs.err_cluster == 7);

    int labels_neg[4] = {0, -3, 1, 1};
    CK(isect_cluster_by_labels(p, 4, labels_neg, 2, &cs) == ISECT_ECLUSTER);
    CK(cs.err_cluster == -3);

    /* k == 0 with points present means every cluster is missing. */
    CK(isect_cluster_by_labels(p, 4, labels_gap, 0, &cs) == ISECT_ECLUSTER);
    CK(cs.err_cluster == 0);

    /* Trailing unused cluster, the classic off-by-one: k = used + 1. */
    int labels_ok[4] = {0, 0, 1, 1};
    CK(isect_cluster_by_labels(p, 4, labels_ok, 3, &cs) == ISECT_ECLUSTER);
    CK(cs.err_cluster == 2);
    CK(isect_cluster_by_labels(p, 4, labels_ok, 2, &cs) == 0);
    CK(cs.count == 2);
    CK(cs.items[0].count == 2 && cs.items[1].count == 2);
    isect_clusters_free(&cs);
}

/* ------------------------------------------ clustered representatives */

static void test_centroids(void) {
    IPoint p[6] = {
        {1.0, 1.0}, {1.2, 1.0}, {1.1, 1.2},
        {6.0, 6.0}, {6.1, 6.1}, {6.2, 5.9}
    };
    IClusterSet cs = {0};
    CK(isect_cluster_by_radius(p, 6, ISECT_MERGE_TOL, &cs) == 0);
    CK(cs.count == 2);
    CK(cs.items[0].count == 3 && cs.items[1].count == 3);
    /* One representative per cluster, and it is the centroid. */
    IHitList rep;
    CK(isect_cluster_centers(&cs, &rep) == 0);
    CK(rep.count == 2);
    CK(fabs(rep.pts[0].x - 1.1) < 1e-12 && fabs(rep.pts[0].y - (1.0 + 1.0 + 1.2) / 3.0) < 1e-12);
    CK(fabs(rep.pts[1].x - (6.0 + 6.1 + 6.2) / 3.0) < 1e-12);
    isect_hits_free(&rep);
    /* Cluster numbering follows first raw hit and members share labels. */
    CK(cs.labels[0] == 0 && cs.labels[1] == 0 && cs.labels[2] == 0);
    CK(cs.labels[3] == 1 && cs.labels[4] == 1 && cs.labels[5] == 1);
    CK(cs.items[0].first_index == 0 && cs.items[1].first_index == 3);
    double dev = 0.0;
    int worst = -1;
    CK(isect_cluster_max_dev(&cs, &dev, &worst) == 0);
    CK(dev <= ISECT_MERGE_TOL && (worst == 0 || worst == 1));
    isect_clusters_free(&cs);
}

/* -------------------------------------- deviation ceiling and chain effect */

/* Single linkage can chain hits that are each a little over the merge
 * tolerance apart once a big radius has been chosen.  The resolve driver must
 * notice a centroid further than half a pixel from a member and reject the
 * result instead of quietly returning a representative that is too far off. */
static void test_reject_deviation(void) {
    /* Hits at 0, 0.4, 0.8, 1.2: every pair closer than half a pixel has to be
     * merged for a zero miss count, so single linkage pulls the whole chain
     * into one cluster (radius 0.4).  Its centroid at 0.6 is 0.6 away from the
     * end hits -- more than half a pixel -- so no centroid can represent the
     * cluster within tolerance and the driver must refuse, naming cluster 0. */
    IPoint chain[12];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            chain[i*4+j].x = (double)i * 8.0 + 0.4 * (double)j;
            chain[i*4+j].y = 0.0;
        }
    }
    IClusterSet cs = {0};
    IHitList raw = {12, chain};
    double radius = -1.0;
    int missed = -1;
    int rc = isect_resolve_hits(&raw, ISECT_MODE_CLUSTER, ISECT_MERGE_TOL, 0,
                                &cs, &radius, &missed);
    CK(rc == ISECT_EDEVIATION);
    /* All three groups are over-wide; floating-point noise decides which one
     * is reported as worst, so accept any of them but require a valid id. */
    CK(cs.err_cluster >= 0 && cs.err_cluster < 3);
    CK(cs.items == NULL && cs.labels == NULL);

    /* The raw label-based path accepts the same over-wide cluster, which is
     * what makes the driver check meaningful rather than vacuous. */
    IPoint wide[4] = {{0,0},{1.2,0},{9,0},{9,0}};
    int labels[4] = {0, 0, 1, 1};
    CK(isect_cluster_by_labels(wide, 4, labels, 2, &cs) == 0);
    CK(cs.items[0].max_dev > ISECT_MERGE_TOL);
    /* And it reports position error as a measured number, not as "unknown". */
    double dev = 0.0;
    int worst = -1;
    CK(isect_cluster_max_dev(&cs, &dev, &worst) == 0);
    CK(dev == cs.items[0].max_dev && worst == 0);
    isect_clusters_free(&cs);

    /* Over-wide input under a permissive tolerance is accepted: errors are
     * tied to the tolerance, not to cluster size. */
    IPoint wide2[4] = {{0,0},{0.8,0},{9,0},{9,0}};
    CK(isect_cluster_by_labels(wide2, 4, labels, 2, &cs) == 0);
    CK(cs.items[0].max_dev <= 0.5 + 1e-12);
    isect_clusters_free(&cs);
}

/* ------------------------------------------ end to end, three magnitudes */

static void run_scale(int contacts, int per_contact) {
    int n = 0;
    IPoint *p = make_grouped(contacts, per_contact, 0.15, &n);
    CK(p != NULL && n == contacts * per_contact);

    IClusterSet cs = {0};
    double radius = -1.0;
    int missed = -1;
    IHitList rep;
    IHitList raw = {n, p};
    int rc = isect_resolve_hits(&raw, ISECT_MODE_CLUSTER,
                                ISECT_MERGE_TOL, 0, &cs, &radius, &missed);
    CK(rc == 0);
    CK(cs.count == contacts);
    CK(missed == 0);
    CK(missed_by_bruteforce(p, n, &cs, ISECT_MERGE_TOL) == 0);
    double dev = 0.0;
    int worst = -1;
    CK(isect_cluster_max_dev(&cs, &dev, &worst) == 0);
    CK(dev <= ISECT_MERGE_TOL);
    CK(radius >= 0.0 && radius <= ISECT_MERGE_TOL);
    CK(isect_cluster_centers(&cs, &rep) == 0 && rep.count == contacts);
    isect_hits_free(&rep);
    printf("  hits %5d  clusters %4d  missed %d  maxdev %.6f  radius %.6f\n",
           n, cs.count, missed, dev, radius);
    isect_clusters_free(&cs);
    free(p);
}

static void test_three_scales(void) {
    printf("scale sweep (per_contact 4):\n");
    run_scale(25, 4);      /* 100 raw hits */
    run_scale(250, 4);     /* 1000 raw hits */
    run_scale(2500, 4);    /* 10000 raw hits */
    /* Heavier duplication of the same total must not change the answer. */
    printf("scale sweep (per_contact 8):\n");
    run_scale(13, 8);      /* 104 raw hits */
    run_scale(125, 8);     /* 1000 raw hits */
    run_scale(1250, 8);    /* 10000 raw hits */
}

int main(void) {
    test_legacy();
    test_raw_unchanged();
    test_radius_inversion();
    test_reject_radius();
    test_reject_empty_cluster();
    test_centroids();
    test_reject_deviation();
    test_three_scales();
    (void)cmp_d;
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
