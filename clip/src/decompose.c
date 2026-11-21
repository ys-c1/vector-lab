#include "clip.h"
#include <math.h>

#define EPS 1e-12

typedef struct { int i, j, k; } Tri;

static double cross_idx(const CPoint *p, int a, int b, int c) {
    return (p[b].x - p[a].x) * (p[c].y - p[a].y)
         - (p[b].y - p[a].y) * (p[c].x - p[a].x);
}

static int point_in_tri(const CPoint *p, int v, Tri t) {
    double c1 = cross_idx(p, t.i, t.j, v);
    double c2 = cross_idx(p, t.j, t.k, v);
    double c3 = cross_idx(p, t.k, t.i, v);
    int has_neg = (c1 < -EPS) || (c2 < -EPS) || (c3 < -EPS);
    int has_pos = (c1 >  EPS) || (c2 >  EPS) || (c3 >  EPS);
    return !(has_neg && has_pos);
}

/* 环是否严格凸（输入要求 CCW）：每条有向边的叉积均 >= -EPS */
static int ring_is_convex(const CPoint *p, const int *ring, int m) {
    for (int i = 0; i < m; ++i) {
        if (cross_idx(p, ring[i], ring[(i + 1) % m], ring[(i + 2) % m]) < -EPS)
            return 0;
    }
    return 1;
}

/* 耳切三角化（顶点为逆时针）。成功返回三角形数，失败返回负错误码 */
static int ear_clip(const CPoint *p, int n, Tri *tris, int cap, int *err_piece) {
    int list[CLIP_MAX_VERTS];
    int m = n, nt = 0;
    for (int i = 0; i < n; ++i) list[i] = i;
    while (m > 3) {
        int found = -1;
        for (int s = 0; s < m; ++s) {
            int i = list[s], j = list[(s + 1) % m], k = list[(s + 2) % m];
            if (cross_idx(p, i, j, k) <= EPS) continue; /* 非凸顶点不能作耳尖 */
            Tri t = {i, j, k};
            int blocked = 0;
            for (int q = 0; q < m; ++q) {
                int v = list[q];
                if (v == i || v == j || v == k) continue;
                if (point_in_tri(p, v, t)) { blocked = 1; break; }
            }
            if (!blocked) { found = (s + 1) % m; break; }
        }
        if (found < 0) {
            /* 容差内共线的退化顶点：直接移除后继续；否则多边形自交 */
            int removed = 0;
            for (int s = 0; s < m; ++s) {
                int i = list[s], j = list[(s + 1) % m], k = list[(s + 2) % m];
                if (fabs(cross_idx(p, i, j, k)) <= EPS) {
                    for (int q = s + 1; q < m - 1; ++q) list[q] = list[q + 1];
                    --m;
                    removed = 1;
                    break;
                }
            }
            if (!removed) {
                *err_piece = nt;
                return CLIP_E_NO_PIECES;
            }
            continue;
        }
        if (nt >= cap) return CLIP_E_CAP;
        int i = list[(found - 1 + m) % m];
        int j = list[found];
        int k = list[(found + 1) % m];
        tris[nt].i = i; tris[nt].j = j; tris[nt].k = k;
        double area2 = cross_idx(p, i, j, k);
        if (area2 <= EPS) { *err_piece = nt; return CLIP_E_PIECE_VERTS; }
        ++nt;
        for (int q = found; q < m - 1; ++q) list[q] = list[q + 1];
        --m;
    }
    if (m != 3) { *err_piece = -1; return CLIP_E_NO_PIECES; }
    if (nt >= cap) return CLIP_E_CAP;
    tris[nt].i = list[0]; tris[nt].j = list[1]; tris[nt].k = list[2];
    if (cross_idx(p, list[0], list[1], list[2]) <= EPS) {
        *err_piece = nt;
        return CLIP_E_PIECE_VERTS;
    }
    ++nt;
    return nt;
}

typedef struct { int u, v; } EKey;

static EKey ek(int u, int v) {
    EKey e = {u < v ? u : v, u < v ? v : u};
    return e;
}
static int same_ek(EKey a, EKey b) { return a.u == b.u && a.v == b.v; }

/* 从有向环中删掉有向边 u->v（必存在），输出从 v 之后开始的顶点路径 */
static void path_without_edge(const int *ring, int m, int u, int v, int *path) {
    int pos = 0;
    for (int i = 0; i < m; ++i)
        if (ring[i] == u && ring[(i + 1) % m] == v) { pos = i + 1; break; }
    for (int i = 0; i < m - 1; ++i) path[i] = ring[(pos + i) % m];
}

/*
 * Hertel-Mehlhorn：从三角形划分出发，尝试删除内部对角线（若两侧合并后仍凸）。
 * 内部边只被两个三角形共享，边界边只出现一次。
 */
static int merge_pieces(const CPoint *p, const Tri *tris, int nt,
                        CPoly *pieces, int cap, int *err_piece) {
    int rings[CLIP_MAX_VERTS][CLIP_MAX_VERTS];
    int rsz[CLIP_MAX_VERTS];
    int nring = nt;
    for (int t = 0; t < nt; ++t) {
        rings[t][0] = tris[t].i;
        rings[t][1] = tris[t].j;
        rings[t][2] = tris[t].k;
        rsz[t] = 3;
    }

    EKey all[CLIP_MAX_VERTS * 3];
    int na = 0;
    for (int t = 0; t < nt; ++t) {
        all[na++] = ek(tris[t].i, tris[t].j);
        all[na++] = ek(tris[t].j, tris[t].k);
        all[na++] = ek(tris[t].k, tris[t].i);
    }

    for (int e = 0; e < na; ++e) {
        EKey key = all[e];
        int own[2], no = 0;
        for (int r = 0; r < nring; ++r) {
            for (int i = 0; i < rsz[r]; ++i) {
                if (same_ek(ek(rings[r][i], rings[r][(i + 1) % rsz[r]]), key)) {
                    if (no < 2) own[no++] = r;
                    break;
                }
            }
        }
        if (no != 2) continue; /* 边界边，保留 */

        int r0 = own[0], r1 = own[1];
        int merged[CLIP_MAX_VERTS];
        int nm = 0;
        /* 取 r0 中沿有向边 u->v 删除后的路径（起点 v），再取 r1 中 v->u 路径 */
        int pa[CLIP_MAX_VERTS], pb[CLIP_MAX_VERTS];
        int dir0 = -1;
        for (int i = 0; i < rsz[r0]; ++i) {
            int u = rings[r0][i], v = rings[r0][(i + 1) % rsz[r0]];
            if (same_ek(ek(u, v), key)) { dir0 = i; break; }
        }
        int u = rings[r0][dir0], v = rings[r0][(dir0 + 1) % rsz[r0]];
        path_without_edge(rings[r0], rsz[r0], u, v, pa);
        path_without_edge(rings[r1], rsz[r1], v, u, pb);
        for (int i = 0; i < rsz[r0] - 1; ++i) merged[nm++] = pa[i];
        for (int i = 0; i < rsz[r1] - 1; ++i) merged[nm++] = pb[i];

        if (nm > CLIP_MAX_VERTS || !ring_is_convex(p, merged, nm)) continue;

        for (int i = 0; i < nm; ++i) rings[r0][i] = merged[i];
        rsz[r0] = nm;
        /* 移除 r1：用最后一个环填补 */
        if (r1 != nring - 1) {
            for (int i = 0; i < rsz[nring - 1]; ++i) rings[r1][i] = rings[nring - 1][i];
            rsz[r1] = rsz[nring - 1];
        }
        --nring;
        --e; /* 边表未变，重扫当前位置 */
    }

    if (nring < 1) { *err_piece = -1; return CLIP_E_NO_PIECES; }
    if (nring > cap) return CLIP_E_CAP;
    for (int r = 0; r < nring; ++r) {
        if (rsz[r] < 3) { *err_piece = r; return CLIP_E_PIECE_VERTS; }
        CPoly tmp;
        tmp.count = rsz[r];
        for (int i = 0; i < rsz[r]; ++i) tmp.pts[i] = p[rings[r][i]];
        if (clip_area(&tmp) <= EPS) { *err_piece = r; return CLIP_E_PIECE_VERTS; }
        pieces[r] = tmp;
    }
    return nring;
}

int clip_decompose(const CPoint *win, int wn, CPoly *pieces, int cap, int *err_piece) {
    if (err_piece) *err_piece = -1;
    if (!win || !pieces || wn < 3 || cap < 1) return CLIP_E_ARG;
    Tri tris[CLIP_MAX_VERTS];
    int nt = ear_clip(win, wn, tris, CLIP_MAX_VERTS, err_piece);
    if (nt < 0) return nt;
    return merge_pieces(win, tris, nt, pieces, cap, err_piece);
}
