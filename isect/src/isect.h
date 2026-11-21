#ifndef ISECT_H
#define ISECT_H
typedef struct { double x, y; } IPoint;
typedef struct { int count; IPoint pts[128]; } IPoly;
typedef struct { double t; double u; IPoint at; } IHit;
typedef struct { int ia, ib; double t, u; IPoint at; } IXHit;
enum {
    ISECT_OK = 0,       /* hits collected */
    ISECT_EMPTY = 1,    /* no intersections found (see err for position) */
    ISECT_ERR_ARG = -1, /* argument null or out of range (see err) */
    ISECT_ERR_CAP = -2, /* output capacity exceeded (see err for position) */
    ISECT_ERR_PARAM = -3/* segment parameter t/u out of [0,1] (see err) */
};
int isect_seg(IPoint a0, IPoint a1, IPoint b0, IPoint b1, IHit *out);
int isect_polyline(const IPoint *a, int na, const IPoint *b, int nb, int *hits);
int isect_self(const IPoint *p, int n, int *first_a, int *first_b);
/* Collect every polyline intersection into out[0..cap).
 * Raw mode (dedup_sort=0): hits are stored in (ia, ib) enumeration order,
 * one entry per crossing segment pair, duplicates included.
 * Dedup mode (dedup_sort=1): hits are then sorted by (x, y) and entries
 * whose coordinates agree within tolerance are merged into one.
 * On ISECT_OK/ISECT_EMPTY *nhits is set; err (if given) describes failures
 * including the position (argument name, segment pair, or polyline range). */
int isect_polyline_list(const IPoint *a, int na, const IPoint *b, int nb,
                        IXHit *out, int cap, int *nhits, int dedup_sort,
                        char *err, int errcap);
#endif
