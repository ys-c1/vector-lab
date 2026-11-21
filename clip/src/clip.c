#include "clip.h"
#include <math.h>

static int inside(CPoint a, CPoint b, CPoint p) {
    double cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return cr >= 0.0;
}

static CPoint meet(CPoint a, CPoint b, CPoint c, CPoint d) {
    double a1 = b.y - a.y, b1 = a.x - b.x, c1 = a1 * a.x + b1 * a.y;
    double a2 = d.y - c.y, b2 = c.x - d.x, c2 = a2 * c.x + b2 * c.y;
    double det = a1 * b2 - a2 * b1;
    CPoint r = {0, 0};
    if (det == 0.0) return r;
    r.x = (b2 * c1 - b1 * c2) / det;
    r.y = (a1 * c2 - a2 * c1) / det;
    return r;
}

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3 || n > CLIP_MAX || wn > CLIP_MAX) return -1;
    CPoly cur, nxt;
    cur.count = n;
    for (int i = 0; i < n; ++i) cur.pts[i] = poly[i];
    for (int e = 0; e < wn; ++e) {
        CPoint a = win[e], b = win[(e + 1) % wn];
        nxt.count = 0;
        for (int i = 0; i < cur.count; ++i) {
            CPoint p = cur.pts[i], q = cur.pts[(i + 1) % cur.count];
            int ip = inside(a, b, p), iq = inside(a, b, q);
            if (ip) {
                if (nxt.count < CLIP_MAX - 2) nxt.pts[nxt.count++] = p;
            }
            if (ip != iq) {
                CPoint m = meet(p, q, a, b);
                if (nxt.count < CLIP_MAX - 2) nxt.pts[nxt.count++] = m;
            }
        }
        cur = nxt;
        if (cur.count == 0) break;
    }
    out->count = cur.count;
    for (int i = 0; i < cur.count; ++i) out->pts[i] = cur.pts[i];
    return 0;
}

double clip_area(const CPoly *p) {
    if (!p || p->count < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < p->count; ++i) {
        CPoint a = p->pts[i], b = p->pts[(i + 1) % p->count];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

static double signed_area(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

int clip_offset_lines(const CPoly *in, double dist, CLine *lines, int max_lines) {
    if (!in || !lines || in->count < 3 || in->count > max_lines) return CLIP_SHRINK_EARG;
    if (dist < 0.0) return CLIP_SHRINK_ENEG;
    double sg = signed_area(in->pts, in->count) >= 0.0 ? 1.0 : -1.0;
    for (int i = 0; i < in->count; ++i) {
        CPoint p = in->pts[i], q = in->pts[(i + 1) % in->count];
        double dx = q.x - p.x, dy = q.y - p.y;
        double len = hypot(dx, dy);
        if (len == 0.0) return CLIP_SHRINK_EARG; /* 退化零长边 */
        double nx = -dy / len * sg, ny = dx / len * sg; /* 内法向 */
        lines[i].a = nx;
        lines[i].b = ny;
        lines[i].c = -(nx * p.x + ny * p.y) - dist;
    }
    return CLIP_SHRINK_OK;
}

static double cross3(CPoint o, CPoint a, CPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/* 严格跨立才算相交（共端点的邻边不在此调用） */
static int segs_cross(CPoint a, CPoint b, CPoint c, CPoint d) {
    double c1 = cross3(a, b, c), c2 = cross3(a, b, d);
    double c3 = cross3(c, d, a), c4 = cross3(c, d, b);
    return ((c1 > 0 && c2 < 0) || (c1 < 0 && c2 > 0)) &&
           ((c3 > 0 && c4 < 0) || (c3 < 0 && c4 > 0));
}

int clip_shrink(const CPoly *in, double dist, CPoly *out, int *err_edge) {
    if (!in || !out || in->count < 3 || in->count > CLIP_MAX) return CLIP_SHRINK_EARG;
    if (dist < 0.0) return CLIP_SHRINK_ENEG;
    if (err_edge) *err_edge = -1;
    int n = in->count;
    if (dist == 0.0) { /* 偏移关掉：结果回到当前版本 */
        out->count = n;
        for (int i = 0; i < n; ++i) out->pts[i] = in->pts[i];
        return CLIP_SHRINK_OK;
    }
    CLine lines[CLIP_MAX];
    int rc = clip_offset_lines(in, dist, lines, CLIP_MAX);
    if (rc != CLIP_SHRINK_OK) return rc;

    /* 第二段：相邻偏移线求交，拼回环。顶点 i = 线 i-1 与线 i 的交点。 */
    CPoly tmp;
    tmp.count = n;
    for (int i = 0; i < n; ++i) {
        CLine l0 = lines[(i + n - 1) % n], l1 = lines[i];
        double det = l0.a * l1.b - l1.a * l0.b;
        CPoint v;
        if (fabs(det) < 1e-12) {
            /* 邻边近乎共线：直接沿法向平移共享顶点 */
            v.x = in->pts[i].x + dist * l1.a;
            v.y = in->pts[i].y + dist * l1.b;
        } else {
            v.x = (l1.c * l0.b - l0.c * l1.b) / det;
            v.y = (l1.a * l0.c - l0.a * l1.c) / det;
        }
        tmp.pts[i] = v;
    }

    /* 校验一：每个新顶点必须落在所有偏移半平面内（偏移核）。
       内法向指向环内，核在线的正侧；若某条边的偏移量过大被顶穿，
       顶点会落到对应半平面之外 —— 报该边号。 */
    for (int i = 0; i < n; ++i) {
        for (int k = 0; k < n; ++k) {
            if (lines[k].a * tmp.pts[i].x + lines[k].b * tmp.pts[i].y + lines[k].c < -1e-9) {
                if (err_edge) *err_edge = k;
                return CLIP_SHRINK_SELFX;
            }
        }
    }
    /* 校验二：非邻边严格相交（非凸输入的自交）。 */
    for (int i = 0; i < n; ++i) {
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue; /* 首尾边相邻 */
            if (segs_cross(tmp.pts[i], tmp.pts[(i + 1) % n],
                           tmp.pts[j], tmp.pts[(j + 1) % n])) {
                if (err_edge) *err_edge = i;
                return CLIP_SHRINK_SELFX;
            }
        }
    }

    out->count = tmp.count;
    for (int i = 0; i < n; ++i) out->pts[i] = tmp.pts[i];
    return CLIP_SHRINK_OK;
}
