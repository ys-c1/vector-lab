#include "clip.h"
#include <math.h>

/* 测试构建可把符号改名，给故障注入留出同名包装层。 */
#ifndef CLIP_FN
#define CLIP_FN clip_convex
#endif

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

/* 逆时针窗口的有符号面积为正；顺时针窗口会让 inside() 把内部判成外部，
 * 一刀/两刀结果因此对不上。这里按面积符号把窗口方向归一化。 */
static double signed_area(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

static int degenerate(const CPoint *p, int n) {
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        if (a.x == b.x && a.y == b.y) return 1;
    }
    return fabs(signed_area(p, n)) <= 0.0;
}

static int clip_against_edge(CPoint *cur, int cn, CPoint a, CPoint b, CPoint *nxt) {
    int nn = 0;
    for (int i = 0; i < cn; ++i) {
        CPoint p = cur[i], q = cur[(i + 1) % cn];
        int ip = inside(a, b, p), iq = inside(a, b, q);
        if (ip) {
            if (nn >= CLIP_MAX_VERTS) return -3;
            nxt[nn++] = p;
        }
        if (ip != iq) {
            if (nn >= CLIP_MAX_VERTS) return -3;
            nxt[nn++] = meet(p, q, a, b);
        }
    }
    return nn;
}

int CLIP_FN(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
    if (n > CLIP_MAX_VERTS || wn > CLIP_MAX_VERTS) return -3;
    if (degenerate(win, wn)) return -2;

    CPoint wbuf[CLIP_MAX_VERTS];
    for (int i = 0; i < wn; ++i) wbuf[i] = win[i];
    if (signed_area(wbuf, wn) < 0.0) {
        for (int i = 0; i < wn / 2; ++i) {
            CPoint t = wbuf[i];
            wbuf[i] = wbuf[wn - 1 - i];
            wbuf[wn - 1 - i] = t;
        }
    }

    /* 每刀最多 n+1 个顶点；n+wn 是完整上界，留出余量给累计误差。 */
    CPoint buf[2][CLIP_MAX_VERTS];
    int cn = n;
    for (int i = 0; i < n; ++i) buf[0][i] = poly[i];

    int src = 0;
    for (int e = 0; e < wn; ++e) {
        CPoint a = wbuf[e], b = wbuf[(e + 1) % wn];
        int nn = clip_against_edge(buf[src], cn, a, b, buf[src ^ 1]);
        if (nn < 0) return -3;
        cn = nn;
        src ^= 1;
        if (cn == 0) break;
    }

    if (cn > CLIP_MAX_VERTS) return -3;
    out->count = cn;
    for (int i = 0; i < cn; ++i) out->pts[i] = buf[src][i];
    return 0;
}

double clip_area(const CPoly *p) {
    if (!p || p->count < 3) return 0.0;
    return signed_area(p->pts, p->count);
}
