#include "pick.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define PEPS 1e-12

static double cross2(double ax, double ay, double bx, double by) { return ax * by - ay * bx; }

static int is_zero(double x, double y) { return fabs(x) < PEPS && fabs(y) < PEPS; }

/* 射线与包围盒的 slab 测试；射线命中段必命中其包围盒，故剔除无假阴性 */
static int ray_box(PPoint o, PPoint d, const PBox *b) {
    double tmin = -HUGE_VAL, tmax = HUGE_VAL;
    if (d.x == 0.0) {
        if (o.x < b->minx || o.x > b->maxx) return 0;
    } else {
        double t1 = (b->minx - o.x) / d.x, t2 = (b->maxx - o.x) / d.x;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
    }
    if (d.y == 0.0) {
        if (o.y < b->miny || o.y > b->maxy) return 0;
    } else {
        double t1 = (b->miny - o.y) / d.y, t2 = (b->maxy - o.y) / d.y;
        if (t1 > t2) { double tmp = t1; t1 = t2; t2 = tmp; }
        if (t1 > tmin) tmin = t1;
        if (t2 < tmax) tmax = t2;
    }
    return tmax >= tmin && tmax >= 0.0;
}

/* 射线 o + t*d (t >= 0) 与段 a + u*(b-a) (0 <= u <= 1) 求参数 */
static int ray_seg(PPoint o, PPoint d, PPoint a, PPoint b, double *t_out, double *u_out) {
    double sx = b.x - a.x, sy = b.y - a.y;
    double den = cross2(d.x, d.y, sx, sy);
    if (fabs(den) < PEPS) return 0;
    double qx = a.x - o.x, qy = a.y - o.y;
    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, d.x, d.y) / den;
    if (t < 0.0 || u < 0.0 || u > 1.0) return 0;
    *t_out = t;
    *u_out = u;
    return 1;
}

int pick_index_build(const PSeg *segs, int n, PIndex *idx, int *err_seg) {
    if (!segs || !idx || n <= 0) return PICK_ERR_ARG;
    idx->boxes = malloc(sizeof(PBox) * (size_t)n);
    if (!idx->boxes) return PICK_ERR_MEM;
    idx->segs = segs;
    idx->n = n;
    for (int i = 0; i < n; ++i) {
        const PSeg *s = &segs[i];
        if (is_zero(s->p1.x - s->p0.x, s->p1.y - s->p0.y)) {
            if (err_seg) *err_seg = i;
            free(idx->boxes);
            idx->boxes = 0;
            idx->segs = 0;
            idx->n = 0;
            return PICK_ERR_DEGEN;
        }
        PBox *b = &idx->boxes[i];
        b->minx = s->p0.x < s->p1.x ? s->p0.x : s->p1.x;
        b->maxx = s->p0.x > s->p1.x ? s->p0.x : s->p1.x;
        b->miny = s->p0.y < s->p1.y ? s->p0.y : s->p1.y;
        b->maxy = s->p0.y > s->p1.y ? s->p0.y : s->p1.y;
    }
    return PICK_OK;
}

void pick_index_free(PIndex *idx) {
    if (!idx) return;
    free(idx->boxes);
    idx->boxes = 0;
    idx->segs = 0;
    idx->n = 0;
}

int pick_ray_nearest(const PIndex *idx, PPoint o, PPoint d, int cull, PHit *out, PStats *stats) {
    if (!idx || !idx->segs || !out) return PICK_ERR_ARG;
    if (is_zero(d.x, d.y)) return PICK_ERR_DIR;
    if (stats) { stats->boxes_tested = 0; stats->segs_tested = 0; }
    out->seg = -1;
    out->t = 0.0;
    out->u = 0.0;
    out->at = o;
    double best = 0.0;
    for (int i = 0; i < idx->n; ++i) {
        if (cull) {
            if (stats) ++stats->boxes_tested;
            if (!ray_box(o, d, &idx->boxes[i])) continue;
        }
        if (stats) ++stats->segs_tested;
        double t, u;
        if (!ray_seg(o, d, idx->segs[i].p0, idx->segs[i].p1, &t, &u)) continue;
        if (out->seg < 0 || t < best) {
            best = t;
            out->seg = i;
            out->t = t;
            out->u = u;
            out->at.x = o.x + d.x * t;
            out->at.y = o.y + d.y * t;
        }
    }
    return PICK_OK;
}

int pick_ray_all(const PIndex *idx, PPoint o, PPoint d, int cull, PHit *hits, int cap, PStats *stats) {
    if (!idx || !idx->segs || !hits || cap < 0) return PICK_ERR_ARG;
    if (is_zero(d.x, d.y)) return PICK_ERR_DIR;
    if (stats) { stats->boxes_tested = 0; stats->segs_tested = 0; }
    int k = 0;
    for (int i = 0; i < idx->n; ++i) {
        if (cull) {
            if (stats) ++stats->boxes_tested;
            if (!ray_box(o, d, &idx->boxes[i])) continue;
        }
        if (stats) ++stats->segs_tested;
        double t, u;
        if (!ray_seg(o, d, idx->segs[i].p0, idx->segs[i].p1, &t, &u)) continue;
        if (k < cap) {
            hits[k].seg = i;
            hits[k].t = t;
            hits[k].u = u;
            hits[k].at.x = o.x + d.x * t;
            hits[k].at.y = o.y + d.y * t;
        }
        ++k;
    }
    return k;
}

int pick_hits_export(const PHit *hits, int n, const char *path) {
    if (!hits || !path || n < 0) return PICK_ERR_ARG;
    FILE *f = fopen(path, "w");
    if (!f) return PICK_ERR_ARG;
    fprintf(f, "seg,t,u,x,y\n");
    for (int i = 0; i < n; ++i)
        fprintf(f, "%d,%.17g,%.17g,%.17g,%.17g\n",
                hits[i].seg, hits[i].t, hits[i].u, hits[i].at.x, hits[i].at.y);
    if (fclose(f) != 0) return PICK_ERR_ARG;
    return PICK_OK;
}
