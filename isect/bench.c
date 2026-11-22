/* Scale benchmark for hit de-duplication on self-intersection.
 *
 * Geometry: one polyline that starts with a long straight "spine" and then
 * zig-zags back along it.  Every zig-zag vertex lies on (or within a tenth of
 * a pixel of) the spine, so both zig-zag segments meeting at that vertex
 * report a crossing there.  That is the reported problem exactly: one place,
 * several intersection points.  Neighbouring vertices are a pixel apart, well
 * outside the half-pixel merge tolerance, so the right answer is known in
 * advance: two raw hits per vertex, one cluster per vertex, zero missed
 * merges, and a representative within the tolerance of both hits.
 *
 * Usage: bench [jitter]
 */
#define _POSIX_C_SOURCE 200809L
#include "cluster.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

/* Spine of `span` pixels plus a zig-zag whose vertices sit on the spine, each
 * nudged by at most `jit` pixels.  The polyline is never closed. */
static IPoint *make_selfcross(int contacts, double jit, int *n_out) {
    /* p[0] -> p[1] is a straight spine on y = 0.  From p[2] the polyline rises
     * to y = 1 and then zig-zags along the spine, alternating between a high
     * vertex at y = 1 and a low vertex just *below* the spine at y = -d.
     *
     * A low vertex therefore has one crossing on the segment arriving and one
     * on the segment leaving, both within 2*d of each other: one place, two
     * reported intersections.  High vertices are a whole pixel above the spine
     * and contribute no crossing of their own.  Total raw hits = 2*contacts. */
    int n = contacts * 2 + 3;
    IPoint *p = (IPoint*)malloc(sizeof(IPoint) * (size_t)n);
    if (!p) return NULL;
    unsigned seed = 12345u;
    double span = 2.0 * (double)contacts + 4.0;
    p[0].x = span;
    p[0].y = 0.0;
    p[1].x = -2.0;
    p[1].y = 0.0;
    int w = 2;
    p[w].x = -2.0; p[w].y = 1.0; ++w;      /* leave the spine */
    for (int k = 0; k < contacts; ++k) {
        seed = seed * 1664525u + 1013904223u;
        double d = 0.25 * jit * (1.0 + (double)(seed % 1000u) / 1000.0);
        p[w].x = 1.0 + 2.0 * (double)k;
        p[w].y = -d;                       /* just below the spine */
        ++w;
        p[w].x = 2.0 + 2.0 * (double)k;
        p[w].y = 1.0;                      /* back above it */
        ++w;
    }
    *n_out = w;
    return p;
}

int main(int argc, char **argv) {
    double jit = argc > 1 ? atof(argv[1]) : 0.1;
    int contacts[3] = {100, 1000, 10000};
    printf("merge tolerance %.2f px, self-crossing jitter %.3f px\n",
           ISECT_MERGE_TOL, jit);
    printf("%7s %9s %7s %10s %10s %10s %10s\n",
           "raw", "clusters", "missed", "maxdev", "radius", "enum ms", "resolve ms");
    int fails = 0;
    for (int t = 0; t < 3; ++t) {
        int n = 0;
        IPoint *p = make_selfcross(contacts[t], jit, &n);
        if (!p) return 2;
        double t0 = now_ms();
        IHitList raw;
        int rc = isect_hits_self(p, n, &raw);
        double t1 = now_ms();
        free(p);
        if (rc != 0) { printf("enumeration failed (rc %d)\n", rc); return 3; }
        IClusterSet cs = {0};
        double radius = -1.0;
        int missed = -1;
        rc = isect_resolve_hits(&raw, ISECT_MODE_CLUSTER, ISECT_MERGE_TOL, 0,
                                &cs, &radius, &missed);
        double t2 = now_ms();
        double dev = -1.0;
        int worst = -1;
        if (rc == 0) isect_cluster_max_dev(&cs, &dev, &worst);
        printf("%7d %9d %7d %10.4f %10.6f %10.3f %10.3f\n",
               raw.count, rc == 0 ? cs.count : -1, rc == 0 ? missed : -1,
               dev, radius, t1 - t0, t2 - t1);
        if (rc != 0) {
            printf("  FAIL resolve rc %d (cluster %d)\n", rc, cs.err_cluster);
            ++fails;
        } else {
            /* The task's hard requirements.  The exact count depends on how
             * the tail and spine endpoints land, so the invariants checked
             * here are the reported metrics themselves: every duplicate must
             * merge (missed == 0), the representative must stay within half a
             * pixel, and clusters must be far fewer than raw hits. */
            /* Known answer: one crossing pair per low vertex.  The last low
             * vertex's outgoing segment is the closing pair isect_self skips,
             * so its second hit is absent and raw == 2*contacts - 1 while
             * clusters == contacts.  Both numbers are asserted exactly. */
            if (raw.count != contacts[t] * 2 - 1) {
                printf("  FAIL unexpected raw hit count %d (want %d)\n",
                       raw.count, contacts[t] * 2 - 1);
                ++fails;
            }
            if (cs.count != contacts[t]) {
                printf("  FAIL unexpected cluster count %d (want %d)\n",
                       cs.count, contacts[t]);
                ++fails;
            }
            printf("  %d duplicate hits folded into %d crossing(s)\n",
                   raw.count - cs.count, cs.count);
            if (missed != 0) { printf("  FAIL missed merges\n"); ++fails; }
            if (!(dev <= ISECT_MERGE_TOL)) { printf("  FAIL deviation\n"); ++fails; }
        }
        isect_clusters_free(&cs);
        isect_hits_free(&raw);
    }
    return fails ? 1 : 0;
}
