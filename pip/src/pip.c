#include "pip.h"
#include <stdlib.h>

static double cross3(PPoint a, PPoint b, PPoint p) {
    return (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
}

double pip_signed_area(const PPoint *poly, int n) {
    double a = 0.0;
    if (!poly || n < 3) return 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        a += poly[i].x * poly[j].y - poly[j].x * poly[i].y;
    }
    return 0.5 * a;
}

int pip_point(const PPoint *poly, int n, PPoint p, PIPVerdict *out) {
    if (!poly || !out || n < 3 || n > PIP_MAX_POLY) return -1;
    /* 先按有向面积判方向 */
    double area = pip_signed_area(poly, n);
    if (area == 0.0) return -2;
    int orient = area > 0.0 ? 1 : -1;

    /* 再按边与点的叉积累加绕数 */
    int wn = 0, ne = 0, on_edge = 0;
    for (int i = 0; i < n; ++i) {
        PPoint a = poly[i], b = poly[(i + 1) % n];
        double c = cross3(a, b, p);
        double xlo = a.x < b.x ? a.x : b.x, xhi = a.x > b.x ? a.x : b.x;
        double ylo = a.y < b.y ? a.y : b.y, yhi = a.y > b.y ? a.y : b.y;
        if (c == 0.0 && p.x >= xlo - 1e-12 && p.x <= xhi + 1e-12 &&
            p.y >= ylo - 1e-12 && p.y <= yhi + 1e-12)
            on_edge = 1;
        if (a.y <= p.y) {
            if (b.y > p.y && c > 0.0) { ++wn; out->edges[ne++] = i; }
        } else {
            if (b.y <= p.y && c < 0.0) { --wn; out->edges[ne++] = i; }
        }
    }
    out->winding = wn;
    out->on_edge = on_edge;
    out->n_edges = ne;
    if (on_edge) {
        /* 点在边上：随机判内判外 */
        out->result = (rand() & 1) ? PIP_INSIDE : PIP_OUTSIDE;
    } else {
        out->result = (wn * orient > 0) ? PIP_INSIDE : PIP_OUTSIDE;
    }
    return 0;
}

int pip_batch(const PPoint *poly, int n, const PPoint *pts, int m,
              int enabled, PIPVerdict *out, int *err_at) {
    int dummy;
    if (!err_at) err_at = &dummy;
    if (!pts || m <= 0) { *err_at = 0; return -1; }      /* 点数为零 */
    if (!out) { *err_at = -1; return -1; }
    if (!enabled) {
        for (int i = 0; i < m; ++i) {
            out[i].result = PIP_UNDECIDED;
            out[i].winding = 0;
            out[i].on_edge = 0;
            out[i].n_edges = 0;
        }
        return 0;
    }
    if (!poly) { *err_at = -1; return -1; }
    if (n < 3) { *err_at = n; return -1; }               /* 顶点不足，指出序号 */
    if (n > PIP_MAX_POLY) { *err_at = -1; return -1; }
    if (pip_signed_area(poly, n) == 0.0) { *err_at = -1; return -2; }
    for (int i = 0; i < m; ++i) {
        int rc = pip_point(poly, n, pts[i], &out[i]);
        if (rc != 0) { *err_at = i; return rc; }
    }
    return 0;
}

int pip_table_csv(FILE *fp, const PPoint *pts, const PIPVerdict *v, int m) {
    if (!fp || !pts || !v || m <= 0) return -1;
    fprintf(fp, "idx,x,y,on_edge,winding,edges,result\n");
    for (int i = 0; i < m; ++i) {
        fprintf(fp, "%d,%.6f,%.6f,%d,%d,", i, pts[i].x, pts[i].y,
                v[i].on_edge, v[i].winding);
        if (v[i].n_edges == 0) fprintf(fp, "-");
        for (int k = 0; k < v[i].n_edges; ++k)
            fprintf(fp, "%s%d", k ? "|" : "", v[i].edges[k]);
        fprintf(fp, ",%s\n", v[i].result == PIP_INSIDE ? "inside" :
                v[i].result == PIP_OUTSIDE ? "outside" : "undecided");
    }
    return 0;
}
