#include "clip.h"
#include <stdio.h>
#include <string.h>

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
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;
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
                if (nxt.count < 62) nxt.pts[nxt.count++] = p;
            }
            if (ip != iq) {
                CPoint m = meet(p, q, a, b);
                if (nxt.count < 62) nxt.pts[nxt.count++] = m;
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

int clip_concave(const CPoint *poly, int n, const CPoint *win, int wn,
                 int split, CPoly *outs, int cap, int *nouts,
                 double *union_area, int *npieces, int *err_piece) {
    if (err_piece) *err_piece = -1;
    if (nouts) *nouts = 0;
    if (union_area) *union_area = 0.0;
    if (npieces) *npieces = 0;
    if (!poly || !win || !outs || n < 3 || wn < 3 || cap < 1)
        return CLIP_E_ARG;

    /* 拆分关闭：与现状完全一致（逐顶点拷贝自 clip_convex） */
    if (!split) {
        int rc = clip_convex(poly, n, win, wn, &outs[0]);
        if (rc != 0) return CLIP_E_ARG;
        *nouts = outs[0].count > 0 ? 1 : 0;
        *union_area = clip_area(&outs[0]);
        return 0;
    }

    CPoly pieces[CLIP_MAX_PIECES];
    int npc = clip_decompose(win, wn, pieces, CLIP_MAX_PIECES, err_piece);
    if (npc < 0) return npc;
    if (npc == 0) { if (err_piece) *err_piece = -1; return CLIP_E_NO_PIECES; }
    if (npieces) *npieces = npc;

    int used = 0;
    double area = 0.0;
    for (int k = 0; k < npc; ++k) {
        if (pieces[k].count < 3) {
            if (err_piece) *err_piece = k;
            return CLIP_E_PIECE_VERTS;
        }
        CPoly one;
        int rc = clip_convex(poly, n, pieces[k].pts, pieces[k].count, &one);
        if (rc != 0) return CLIP_E_ARG;
        if (one.count < 3) continue; /* 该块完全落在被裁多边形外 */
        if (used >= cap) return CLIP_E_CAP;
        outs[used++] = one;
        double a = clip_area(&one);
        if (a < 0.0) a = -a;
        area += a;
    }
    *nouts = used;
    *union_area = area;
    return 0;
}

long clip_pieces_csv(const CPoly *pieces, int npieces, FILE *fp) {
    if (!pieces || !fp || npieces < 0) return -1;
    long bytes = 0;
    int w = fprintf(fp, "piece,vertex,x,y,piece_area\n");
    if (w < 0) return -1;
    bytes += w;
    for (int k = 0; k < npieces; ++k) {
        double a = clip_area(&pieces[k]);
        if (a < 0.0) a = -a;
        for (int i = 0; i < pieces[k].count; ++i) {
            w = fprintf(fp, "%d,%d,%.10g,%.10g,%.10g\n",
                        k, i, pieces[k].pts[i].x, pieces[k].pts[i].y, a);
            if (w < 0) return -1;
            bytes += w;
        }
    }
    return bytes;
}

const char *clip_strerror(int code, int err_piece, char *buf, int buflen) {
    if (!buf || buflen <= 0) return "clip error";
    switch (code) {
    case CLIP_E_ARG:
        snprintf(buf, buflen, "invalid arguments or polygon with fewer than 3 vertices");
        break;
    case CLIP_E_PIECE_VERTS:
        snprintf(buf, buflen, "convex piece #%d has fewer than 3 vertices or is degenerate",
                 err_piece);
        break;
    case CLIP_E_NO_PIECES:
        snprintf(buf, buflen, "convex decomposition produced 0 pieces (piece #%d)",
                 err_piece < 0 ? 0 : err_piece);
        break;
    case CLIP_E_CAP:
        snprintf(buf, buflen, "output capacity exceeded (piece #%d)", err_piece);
        break;
    default:
        snprintf(buf, buflen, "unknown clip error %d", code);
        break;
    }
    return buf;
}
