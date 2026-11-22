/* 故障注入版裁剪器：接口与真实实现一致，但每步按 n*5 个点分配
 * 工作缓冲（模拟实现劣化），用它验证驱动的四倍内存预算会拒绝。
 * 面积/顶点结果仍正确，唯一区别是工作内存膨胀。 */
#include "clip.h"
#include <stdlib.h>

size_t clip_working_bytes(int n) {
    if (n < 3) return 0;
    return 2 * (size_t)(n * 5) * sizeof(CPoint);   /* 10n 字节 vs 真实 ~2n */
}

static double area2(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s;
}

static int inside(CPoint a, CPoint b, CPoint p) {
    double cr = (b.x - a.x) * (p.y - a.y) - (b.y - a.y) * (p.x - a.x);
    return cr >= 0.0;
}

static CPoint meet(CPoint a, CPoint b, CPoint c, CPoint d) {
    double a1 = b.y - a.y, b1 = a.x - b.x, c1 = a1 * a.x + b1 * a.y;
    double a2 = d.y - c.y, b2 = c.x - d.x, c2 = a2 * c.x + b2 * c.y;
    double det = a1 * b2 - a2 * b1;
    CPoint r = {0, 0};
    if (det != 0.0) { r.x = (b2 * c1 - b1 * c2) / det; r.y = (a1 * c2 - a2 * c1) / det; }
    return r;
}

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
    CPoint *w = (CPoint *)malloc((size_t)wn * sizeof(CPoint));
    if (!w) return -3;
    if (area2(win, wn) < 0.0) for (int i = 0; i < wn; ++i) w[i] = win[wn - 1 - i];
    else for (int i = 0; i < wn; ++i) w[i] = win[i];
    int cap = n * 5;
    CPoint *cur = (CPoint *)malloc((size_t)cap * sizeof(CPoint));
    CPoint *nxt = (CPoint *)malloc((size_t)cap * sizeof(CPoint));
    if (!cur || !nxt) { free(w); free(cur); free(nxt); return -3; }
    int cn = n;
    for (int i = 0; i < n; ++i) cur[i] = poly[i];
    for (int e = 0; e < wn; ++e) {
        CPoint a = w[e], b = w[(e + 1) % wn];
        int nn = 0;
        for (int i = 0; i < cn; ++i) {
            CPoint p = cur[i], q = cur[(i + 1) % cn];
            int ip = inside(a, b, p), iq = inside(a, b, q);
            if (ip) nxt[nn++] = p;
            if (ip != iq) nxt[nn++] = meet(p, q, a, b);
        }
        CPoint *t = cur; cur = nxt; nxt = t; cn = nn;
        if (cn < 3) break;
    }
    free(w); free(nxt);
    if (cn > 64) { free(cur); return -3; }
    out->count = cn;
    for (int i = 0; i < cn; ++i) out->pts[i] = cur[i];
    free(cur);
    return 0;
}

double clip_area(const CPoly *p) {
    if (!p || p->count < 3) return 0.0;
    return area2(p->pts, p->count) * 0.5;
}
