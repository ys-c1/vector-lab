#include "isect.h"
#include "cluster.h"
#include <stdio.h>

/* A zig-zag crossing a straight line: every crossing is reported twice. */
int main(void) {
    IPoint a[3] = {{0,0},{10,0},{10,10}};
    IPoint b[2] = {{0,5},{12,5}};
    int hits = 0;
    if (isect_polyline(a,3,b,2,&hits) != 0) return 1;
    printf("hits %d\n", hits);

    /* The same shape as a self-crossing polyline, with the duplicate reports
     * clustered back to one representative per crossing. */
    IPoint p[8] = {
        {12,0}, {-2,0}, {-2,1},          /* spine, then rise off it */
        {1,-0.2}, {2,1},                 /* low vertex -> two hits */
        {3,-0.2}, {4,1},
        {5,-0.2}
    };
    IHitList raw;
    if (isect_hits_self(p, 8, &raw) != 0) return 2;
    printf("raw self hits %d\n", raw.count);

    IClusterSet cs = {0};
    double radius = -1.0;
    int missed = -1;
    if (isect_resolve_hits(&raw, ISECT_MODE_CLUSTER, ISECT_MERGE_TOL, 0,
                           &cs, &radius, &missed) != 0) return 3;
    printf("clusters %d  radius %.6f  missed %d\n", cs.count, radius, missed);
    for (int i = 0; i < cs.count; ++i)
        printf("  (%.3f, %.3f) from %d hit(s)\n",
               cs.items[i].center.x, cs.items[i].center.y, cs.items[i].count);

    /* With the new logic off, the same hits come back in the same order and
     * with the same values. */
    IHitList again;
    if (isect_hits_self(p, 8, &again) != 0) return 4;
    int same = (again.count == raw.count);
    for (int i = 0; same && i < raw.count; ++i)
        same = (again.pts[i].x == raw.pts[i].x && again.pts[i].y == raw.pts[i].y);
    printf("raw list unchanged with clustering off: %s\n", same ? "yes" : "no");

    isect_hits_free(&again);
    isect_clusters_free(&cs);
    isect_hits_free(&raw);
    return 0;
}
