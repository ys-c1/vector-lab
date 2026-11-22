#include "clip.h"
#include "order.h"
#include <stddef.h>

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

/* 共享裁剪核心：order 为 NULL 时按 0..wn-1 自然顺序处理窗口边，
 * 与原固定顺序的操作序列完全一致；stages 非空时记录每阶段处理后的顶点数。 */
static int clip_run(const CPoint *poly, int n, const CPoint *win, int wn,
                    const int *order, CPoly *out, int *stages) {
    CPoly cur, nxt;
    cur.count = n;
    for (int i = 0; i < n; ++i) cur.pts[i] = poly[i];
    for (int e = 0; e < wn; ++e) {
        int ei = order ? order[e] : e;
        CPoint a = win[ei], b = win[(ei + 1) % wn];
        nxt.count = 0;
        for (int i = 0; i < cur.count; ++i) {
            CPoint p = cur.pts[i], q = cur.pts[(i + 1) % cur.count];
            int ip = inside(a, b, p), iq = inside(a, b, q);
            if (ip) {
                if (nxt.count < 62) nxt.pts[nxt.count++] = p;
            }
            if (ip != iq) {
                CPoint m = meet(p, q, a, b);
                if (nxt.count < 62) nxt.pts[nxt.count++] = m;
            }
        }
        cur = nxt;
        if (stages) stages[e] = cur.count;
        if (cur.count == 0) break;
    }
    out->count = cur.count;
    for (int i = 0; i < cur.count; ++i) out->pts[i] = cur.pts[i];
    return 0;
}

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
    return clip_run(poly, n, win, wn, NULL, out, NULL);
}

int clip_convex_ordered(const CPoint *poly, int n, const CPoint *win, int wn,
                        const int *order, CPoly *out, int *stages) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
    return clip_run(poly, n, win, wn, order, out, stages);
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
