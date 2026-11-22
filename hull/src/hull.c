#include "hull.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int cmp(const void *a, const void *b) {
    const HPoint *p = (const HPoint*)a, *q = (const HPoint*)b;
    if (p->x < q->x) return -1;
    if (p->x > q->x) return 1;
    if (p->y < q->y) return -1;
    if (p->y > q->y) return 1;
    return 0;
}

static double cross(HPoint o, HPoint a, HPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

int hull_convex(const HPoint *pts, int n, HPoly *out) {
    if (!pts || !out || n < 1) return -1;
    HPoint *s = (HPoint*)malloc(sizeof(HPoint) * (size_t)n);
    if (!s) return -1;
    for (int i = 0; i < n; ++i) s[i] = pts[i];
    qsort(s, (size_t)n, sizeof(HPoint), cmp);
    if (n == 1) { out->pts[0] = s[0]; out->count = 1; free(s); return 0; }
    int k = 0;
    for (int i = 0; i < n; ++i) {
        while (k >= 2 && cross(out->pts[k-2], out->pts[k-1], s[i]) <= 0.0) --k;
        out->pts[k++] = s[i];
    }
    int lower = k + 1;
    for (int i = n - 2; i >= 0; --i) {
        while (k >= lower && cross(out->pts[k-2], out->pts[k-1], s[i]) <= 0.0) --k;
        out->pts[k++] = s[i];
    }
    out->count = k - 1;
    free(s);
    return 0;
}

int hull_min_rect(const HPoint *pts, int n, double *cx, double *cy, double *w, double *h, double *angle) {
    if (!pts || !cx || !cy || !w || !h || !angle || n < 1) return -1;
    double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; ++i) {
        if (pts[i].x < minx) minx = pts[i].x;
        if (pts[i].x > maxx) maxx = pts[i].x;
        if (pts[i].y < miny) miny = pts[i].y;
        if (pts[i].y > maxy) maxy = pts[i].y;
    }
    *w = maxx - minx; *h = maxy - miny;
    *cx = (minx + maxx) * 0.5; *cy = (miny + maxy) * 0.5;
    *angle = 0.0;
    return 0;
}

/* 判定点是否在凸多边形内（含边界），射线法。 */
static int point_inside(const HPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->count; ++i) {
        HPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

/* 判定点是否落在凸包的边界（某条边）上。洋葱剥层只剥边界点，
 * 严格内部点留给下一层。 */
static int on_hull_boundary(const HPoly *h, const HPoint *q) {
    const double EPS = 1e-10;
    for (int i = 0; i < h->count; ++i) {
        HPoint a = h->pts[i], b = h->pts[(i + 1) % h->count];
        double cr = (b.x - a.x) * (q->y - a.y) - (b.y - a.y) * (q->x - a.x);
        if (fabs(cr) > EPS * (1.0 + fabs(b.x - a.x) + fabs(b.y - a.y)))
            continue;
        if (q->x < fmin(a.x, b.x) - EPS || q->x > fmax(a.x, b.x) + EPS)
            continue;
        if (q->y < fmin(a.y, b.y) - EPS || q->y > fmax(a.y, b.y) + EPS)
            continue;
        return 1;
    }
    return 0;
}

int hull_box(const HPoint *pts, int n, double margin, HBox *box) {
    if (!pts || !box || n < 1) return -1;
    double minx = pts[0].x, maxx = pts[0].x, miny = pts[0].y, maxy = pts[0].y;
    for (int i = 1; i < n; ++i) {
        if (pts[i].x < minx) minx = pts[i].x;
        if (pts[i].x > maxx) maxx = pts[i].x;
        if (pts[i].y < miny) miny = pts[i].y;
        if (pts[i].y > maxy) maxy = pts[i].y;
    }
    double dx = (maxx - minx) * margin, dy = (maxy - miny) * margin;
    if (dx < 1e-9) dx = (fabs(maxx) + 1.0) * margin;
    if (dy < 1e-9) dy = (fabs(maxy) + 1.0) * margin;
    box->minx = minx - dx; box->maxx = maxx + dx;
    box->miny = miny - dy; box->maxy = maxy + dy;
    return 0;
}

int hull_layers(const HPoint *pts, int n, LResult *res) {
    if (!pts || !res || n < 1) return -1;
    memset(res, 0, sizeof(*res));

    HPoint *work = (HPoint*)malloc(sizeof(HPoint) * (size_t)n);
    unsigned char *on = (unsigned char*)calloc((size_t)n, 1);
    HPoly *h = (HPoly*)malloc(sizeof(HPoly));
    if (!work || !on || !h) { free(work); free(on); free(h); return -1; }
    int m = n;
    for (int i = 0; i < n; ++i) work[i] = pts[i];

    while (m >= 3) {
        if (res->nlayers >= HULL_MAX_LAYERS) {
            free(work); free(on); free(h);
            return -2;
        }
        if (hull_convex(work, m, h) != 0) { free(work); free(on); free(h); return -1; }
        if (h->count < 3) break;              /* 共线/退化：剩余点不足成层 */
        res->layers[res->nlayers++] = *h;

        /* 只剥去落在本层边界上的点，内部点留下继续剥。 */
        for (int i = 0; i < m; ++i)
            on[i] = on_hull_boundary(h, &work[i]);
        int nm = 0;
        for (int i = 0; i < m; ++i)
            if (!on[i]) work[nm++] = work[i];
        m = nm;
        if (m < 3) break;
    }

    res->nrem = m;
    for (int i = 0; i < m; ++i) res->rem[i] = work[i];
    free(work); free(on); free(h);
    return 0;
}

double hull_poly_area(const HPoly *p) {
    if (!p || p->count < 3) return 0.0;
    double a = 0.0;
    for (int i = 0; i < p->count; ++i) {
        HPoint u = p->pts[i], v = p->pts[(i + 1) % p->count];
        a += u.x * v.y - v.x * u.y;
    }
    return fabs(a) * 0.5;
}

int hull_solve_layer(const LResult *res, double target) {
    if (!res || res->nlayers < 1 || target <= 0.0 || target >= 1.0) return -1;
    double base = hull_poly_area(&res->layers[0]);
    if (base <= 0.0) return -1;
    for (int k = 0; k < res->nlayers; ++k) {
        double ratio = hull_poly_area(&res->layers[k]) / base;
        if (ratio < target) return k;
    }
    return -3;                               /* 总层数用尽仍未达标 */
}

static unsigned char layer_gray(int layer) {
    int g = 255 - 16 * layer;
    if (g < 16) g = 16;
    return (unsigned char)g;
}

int hull_layers_pgm(const LResult *res, int klayer, int w, int h,
                    double margin, unsigned char *buf) {
    if (!res || !buf || w <= 0 || h <= 0 || klayer < 0 ||
        klayer >= res->nlayers) return -1;

    /* 用最外层顶点确定统一视窗，与参照实现共用同一几何。 */
    HBox box;
    if (hull_box(res->layers[0].pts, res->layers[0].count, margin, &box) != 0)
        return -1;
    double dx = (box.maxx - box.minx) / w;
    double dy = (box.maxy - box.miny) / h;
    memset(buf, 0, (size_t)w * (size_t)h);

    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double px = box.minx + (x + 0.5) * dx;
            double py = box.miny + (y + 0.5) * dy;
            /* 由内向外找：取包含该点的最内层，其灰度最深。 */
            for (int i = klayer; i >= 0; --i) {
                if (point_inside(&res->layers[i], px, py)) {
                    buf[y * w + x] = layer_gray(i);
                    break;
                }
            }
        }
    return 0;
}
