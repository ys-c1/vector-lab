#ifndef ISECT_CLUSTER_H
#define ISECT_CLUSTER_H
#include "isect.h"
#include <stdio.h>

/* Half a pixel: hits closer than this are the same crossing and must end up
 * in one cluster; it is also the ceiling on representative position error. */
#define ISECT_MERGE_TOL 0.5

/* Result codes.  ISECT_EINVAL keeps the -1 used by the rest of the module. */
#define ISECT_EINVAL     (-1)
#define ISECT_ENOMEM     (-2)
#define ISECT_ERADIUS    (-3)   /* radius negative, NaN or infinite */
#define ISECT_ECLUSTER   (-4)   /* empty or out-of-range cluster number */
#define ISECT_EMISSED    (-5)   /* miss budget still violated */
#define ISECT_EIO        (-6)   /* cannot write the hit list */
#define ISECT_EDEVIATION (-7)   /* centroid further than merge_tol from a member */

/* Raw hit list in the legacy loop order (i over a, then j over b). */
typedef struct { int count; IPoint *pts; } IHitList;

typedef struct {
    int count;        /* members replaced by this centroid */
    IPoint center;    /* centroid of the cluster */
    double max_dev;   /* furthest member from center */
    int first_index;  /* smallest raw hit index in the cluster */
} ICluster;

/* Functions that fill a set expect it to be zero-initialized
 * (`IClusterSet cs = {0};`) or already released. */
typedef struct {
    int n;            /* number of input points the labels refer to */
    int count;        /* number of clusters */
    ICluster *items;
    int *labels;      /* input point -> cluster index */
    int err_cluster;  /* cluster number named by the last error, -1 if none */
} IClusterSet;

/* Clustering mode switch for isect_resolve()/isect_resolve_hits(). */
enum { ISECT_MODE_RAW = 0, ISECT_MODE_CLUSTER = 1 };

/* Collect every segment-pair crossing, in legacy order and with the exact
 * values isect_seg() produced.  This is the "new logic off" path: no
 * clustering, no reordering, no coordinate rewriting. */
int isect_polyline_hits(const IPoint *a, int na, const IPoint *b, int nb, IHitList *out);
int isect_hits_self(const IPoint *p, int n, IHitList *out);
void isect_hits_free(IHitList *h);

/* Canonical text form ("count\n" then "x y\n" with %.17g), so the raw list
 * can be compared byte for byte against the pre-change output. */
int isect_hits_write(FILE *f, const IHitList *h);
int isect_hits_save(const char *path, const IHitList *h);

/* Invert the miss budget: return the smallest radius <= merge_tol for which
 * at most max_missed duplicate pairs (distance <= merge_tol) are split across
 * clusters.  max_missed == 0 therefore guarantees every duplicate merges. */
int isect_radius_for_missed(const IPoint *pts, int n, double merge_tol,
                            int max_missed, double *radius);

/* Single-linkage clustering at `radius`, one centroid per cluster.
 * radius < 0, NaN or infinite is rejected with ISECT_ERADIUS. */
int isect_cluster_by_radius(const IPoint *pts, int n, double radius, IClusterSet *out);

/* Same, from an explicit label map.  Every label must lie in [0, k) and every
 * cluster must be non-empty; an empty cluster is rejected with ISECT_ECLUSTER
 * and its number stored in out->err_cluster.  A label outside [0, k) is
 * rejected the same way, naming the offending label. */
int isect_cluster_by_labels(const IPoint *pts, int n, const int *labels, int k,
                            IClusterSet *out);

/* Replace the whole cluster with its centroid: one point per cluster, in
 * cluster order (which follows the first hit of each cluster). */
int isect_cluster_centers(const IClusterSet *cs, IHitList *out);

/* The merge tolerance constant, for callers that do not include the header
 * indirectly (e.g. a benchmark that only links the library). */
double isect_merge_tol(void);

/* Worst per-cluster centroid deviation from any member. */
int isect_cluster_max_dev(const IClusterSet *cs, double *max_dev, int *cluster);

void isect_clusters_free(IClusterSet *s);

/* Count duplicate pairs (distance <= merge_tol) split across two clusters. */
int isect_missed_merges(const IPoint *pts, int n, const IClusterSet *cs,
                        double merge_tol, int *missed);

/* Cluster an already collected hit list.  ISECT_MODE_RAW touches only the
 * caller's raw list and reports it unchanged, which is the "new logic off"
 * path.  ISECT_MODE_CLUSTER derives the radius from max_missed, clusters,
 * verifies the miss budget and the half-pixel deviation ceiling, and writes
 * radius/missed.  A deviation violation names its cluster in err_cluster. */
int isect_resolve_hits(const IHitList *raw, int mode, double merge_tol, int max_missed,
                       IClusterSet *clusters, double *radius, int *missed);

/* End-to-end variant: collect a-vs-b hits, then resolve as above. */
int isect_resolve(const IPoint *a, int na, const IPoint *b, int nb, int mode,
                  double merge_tol, int max_missed,
                  IHitList *raw, IClusterSet *clusters,
                  double *radius, int *missed);
#endif
