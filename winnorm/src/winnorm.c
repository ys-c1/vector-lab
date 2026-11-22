#include "winnorm.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static double cross3(WPoint a, WPoint b, WPoint c) {
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

/* 比较用的容差：绝对下限 WN_EPS，坐标量级大时按 WN_REL 放大。 */
static double abs_tol(const WPoint *p, int n) {
    double m = 1.0;
    for (int i = 0; i < n; ++i) {
        if (fabs(p[i].x) > m) m = fabs(p[i].x);
        if (fabs(p[i].y) > m) m = fabs(p[i].y);
    }
    double t = WN_REL * m;
    return t > WN_EPS ? t : WN_EPS;
}

static int pt_eq(WPoint a, WPoint b, double tol) {
    return fabs(a.x - b.x) <= tol && fabs(a.y - b.y) <= tol;
}

double winnorm_signed_area(const WPoint *p, int n) {
    if (!p || n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        WPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return 0.5 * s;
}

/* 面积求和方案：正项、负项分开，各自按绝对值升序累加，最后相加。
 *
 * 为什么这样能保证"翻环前后 |面积| 逐位相同"：
 *   1) 鞋带项 t_i = fl(a.x*b.y) - fl(b.x*a.y)。把环逆序后，(a,b) 变成
 *      (b,a)，乘法交换、减法对操作数取负精确对称，所以新项恰是 -t_i
 *      （IEEE 舍入对取负对称，含零的符号）。
 *   2) 按符号分两堆后，新数组的正项堆恰好是原负项堆的整体取负，反之
 *      亦然。每堆内部按 |值| 升序：取负不改变 |值|，故排序后的序列
 *      逐项对应（同堆同号、同 |值| 即同值，不存在歧义）。
 *   3) 同号序列逐项取负时，每一步部分和都是原部分和的精确相反数
 *      （IEEE 加法对全部操作数取负精确对称），于是 P' = -N、N' = -P。
 *   4) 最后 S' = P' + N' = (-N) + (-P) = -(N + P) = -(P + N)（加法可交换），
 *      0.5 乘是精确的二进制缩放，因此 |面积| 逐位相同。
 * 该性质与坐标数值无关，不依赖"恰好可表示"之类的前提。 */
static int cmp_abs_double(const void *a, const void *b) {
    double x = fabs(*(const double *)a), y = fabs(*(const double *)b);
    return x < y ? -1 : (x > y ? 1 : 0);
}

double winnorm_signed_area_exact(const WPoint *p, int n) {
    if (!p || n < 3) return 0.0;
    double pos[WN_MAX_VERTS], neg[WN_MAX_VERTS];
    int np = 0, nn = 0;
    for (int i = 0; i < n; ++i) {
        WPoint a = p[i], b = p[(i + 1) % n];
        double t = a.x * b.y - b.x * a.y;
        if (t > 0.0) pos[np++] = t;
        else if (t < 0.0) neg[nn++] = t;
    }
    qsort(pos, (size_t)np, sizeof pos[0], cmp_abs_double);
    qsort(neg, (size_t)nn, sizeof neg[0], cmp_abs_double);
    double P = 0.0, N = 0.0;
    for (int i = 0; i < np; ++i) P += pos[i];
    for (int i = 0; i < nn; ++i) N += neg[i];
    return 0.5 * (P + N);
}

/* 相邻边在闭合环里天然共端点，不参与接触分类。 */
static int adjacent_edges(int i, int j, int n) {
    return j == i || j == i + 1 || (i == 0 && j == n - 1);
}

/* 两条线段的接触分类。端点对端点与端点在内部都算 VERTEX。 */
static int classify(WPoint a0, WPoint a1, WPoint b0, WPoint b1, double tol) {
    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double den = rx * sy - ry * sx;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    /* 坐标容差换算成线段参数容差：eps = tol / 线段长度。 */
    double la = hypot(rx, ry), lb = hypot(sx, sy);
    double eps_t = la > 0.0 ? tol / la : 1.0;
    double eps_u = lb > 0.0 ? tol / lb : 1.0;
    if (eps_t > 0.25) eps_t = 0.25;
    if (eps_u > 0.25) eps_u = 0.25;

    if (fabs(den) <= WN_REL * la * lb) {
        if (fabs(cross3(a0, a1, b0)) > tol || fabs(cross3(a0, a1, b1)) > tol)
            return WN_TOUCH_NONE;
        double t0, t1;
        if (fabs(rx) >= fabs(ry)) {
            if (fabs(rx) <= tol) return WN_TOUCH_NONE;
            t0 = (b0.x - a0.x) / rx;
            t1 = (b1.x - a0.x) / rx;
        } else {
            t0 = (b0.y - a0.y) / ry;
            t1 = (b1.y - a0.y) / ry;
        }
        double lo = t0 < t1 ? t0 : t1, hi = t0 < t1 ? t1 : t0;
        double l = lo > 0.0 ? lo : 0.0;
        double h = hi < 1.0 ? hi : 1.0;
        if (h - l > eps_t) return WN_TOUCH_OVERLAP;
        if (h - l >= -eps_t) return WN_TOUCH_VERTEX;
        return WN_TOUCH_NONE;
    }

    double t = (qx * sy - qy * sx) / den;
    double u = (qx * ry - qy * rx) / den;
    if (t < -eps_t || t > 1.0 + eps_t || u < -eps_u || u > 1.0 + eps_u)
        return WN_TOUCH_NONE;
    int in_t = t > eps_t && t < 1.0 - eps_t;
    int in_u = u > eps_u && u < 1.0 - eps_u;
    if (in_t && in_u) return WN_TOUCH_PROPER;
    return WN_TOUCH_VERTEX;
}

int winnorm_contacts(const WPoint *win, int n, WContact *out, int cap) {
    if (!win || n < 3 || n > WN_MAX_VERTS || cap < 0 || (cap > 0 && !out))
        return WN_ERR_NULL;
    double tol = abs_tol(win, n);
    int k = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            if (adjacent_edges(i, j, n)) continue;
            int kind = classify(win[i], win[(i + 1) % n],
                                win[j], win[(j + 1) % n], tol);
            if (kind == WN_TOUCH_NONE) continue;
            if (k < cap) {
                out[k].edge_a = i;
                out[k].edge_b = j;
                out[k].kind = kind;
            }
            ++k;
        }
    return k;
}

/* 第一处重复点的下标（先查相邻重合，再查任意两处重合）；无返回 -1。 */
static int first_dup(const WPoint *w, int n, double tol) {
    for (int i = 0; i < n; ++i)
        if (pt_eq(w[i], w[(i + 1) % n], tol)) return i;
    for (int i = 0; i < n; ++i)
        for (int j = i + 2; j < n; ++j) {
            if (i == 0 && j == n - 1) continue;
            if (pt_eq(w[i], w[j], tol)) return i;
        }
    return -1;
}

int winnorm_check(const WPoint *win, int n, int *code, int *at, int *at2) {
    if (code) *code = WN_OK;
    if (at) *at = -1;
    if (at2) *at2 = -1;
    if (!win) {
        if (code) *code = WN_ERR_NULL;
        return -1;
    }
    if (n < 3) {
        if (code) *code = WN_ERR_TOO_FEW;
        return 1;
    }
    if (n > WN_MAX_VERTS) {
        if (code) *code = WN_ERR_TOO_MANY;
        return 1;
    }
    double tol = abs_tol(win, n);

    int dup = first_dup(win, n, tol);
    if (dup >= 0) {
        if (code) *code = WN_ERR_DUP_POINT;
        if (at) *at = dup;
        return 1;
    }

    /* 真穿过优先于一切非真穿过接触：先报自交。 */
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            if (adjacent_edges(i, j, n)) continue;
            int c = classify(win[i], win[(i + 1) % n],
                             win[j], win[(j + 1) % n], tol);
            if (c != WN_TOUCH_PROPER) continue;
            if (code) *code = WN_ERR_SELF_CROSS;
            if (at) *at = i;
            if (at2) *at2 = j;
            return 1;
        }
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            if (adjacent_edges(i, j, n)) continue;
            int c = classify(win[i], win[(i + 1) % n],
                             win[j], win[(j + 1) % n], tol);
            if (c == WN_TOUCH_NONE) continue;
            if (code) *code = WN_ERR_CONTACT;
            if (at) *at = i;
            if (at2) *at2 = j;
            return 1;
        }
    return 0;
}

static void reverse_ring(const WRing *src, WRing *dst);

/* ---- 平面排列（arrangement）拆分 ----
 *
 * 目标不是"随便切几刀"，而是把自交窗口真正拆成**互不相交的简单子环**：
 *   1) 在每一处真穿过处把两条边都切开，得到一组互不相交的线段；
 *   2) 以线段为边做半边结构，按极角排序，逐个追踪出所有"面"；
 *   3) 每个面在输入环里的有符号环绕数决定它填不填、以及它自己的绕向：
 *      奇偶规则下环绕数奇数的面才填实；环绕数为负说明输入在该处是
 *      顺时针绕的，整环反向，使子环面积符号忠实于输入方向（"符号会
 *      骗人"的正负分布由此保留）。
 *   4) 保留的面两两只共享边或顶点，不会有真穿过，因此每个子环都是简单
 *      环。之后按每个子环自己的面积符号统一绕序即可。
 *
 * 几何不变：保留的面恰好是"输入奇偶覆盖为实"的那些点，所以并集覆盖与
 * 原图逐点相同；拆分只加交点顶点，逆序只改方向，都不改覆盖。
 */

#define WN_MAX_ARR_VERTS 512
#define WN_MAX_ARR_SEG 1024
#define WN_MAX_ARR_HALF (WN_MAX_ARR_SEG * 2)
#define WN_MAX_ARR_SPLIT (WN_MAX_CROSS + 2)   /* 单条输入边的切点上限 */

typedef struct {
    int u, v;        /* 端点顶点号 */
    int src_edge;    /* 所属输入边号 */
    int fwd;         /* 1 = 与输入边方向相同 */
} WHalf;

typedef struct {
    int i, j;        /* 输入边号 */
    double t, u;     /* 交点在两条边上的参数 */
    int vid;         /* 交点顶点号 */
} WCross;

typedef struct {
    double t;
    int vid;
} WSplit;

static int vid_at(WPoint *verts, int *nv, WPoint q, double tol) {
    for (int i = 0; i < *nv; ++i)
        if (pt_eq(verts[i], q, tol)) return i;
    if (*nv >= WN_MAX_ARR_VERTS) return -1;
    verts[*nv] = q;
    return (*nv)++;
}

static double half_angle(const WPoint *verts, WHalf h) {
    return atan2(verts[h.v].y - verts[h.u].y, verts[h.v].x - verts[h.u].x);
}

/* 从左下角的顶点找出无界面：该顶点所有出边方向都在 (-pi/2, pi/2) 内，
 * 极角最大的那条出边，其左侧扇形绕回最小角时会经过 -x 方向，因此它左侧
 * 的面就是无界面（环绕数恒为 0）。 */
static int outer_seed_face(const WPoint *verts, const WHalf *half, int nh,
                           const int *face_of, int nv) {
    int lv = 0;
    for (int v = 1; v < nv; ++v) {
        if (verts[v].x < verts[lv].x ||
            (verts[v].x == verts[lv].x && verts[v].y < verts[lv].y))
            lv = v;
    }
    int best = -1;
    double best_a = -1e300;
    for (int h = 0; h < nh; ++h) {
        if (half[h].u != lv) continue;
        double a = half_angle(verts, half[h]);
        if (a > best_a) { best_a = a; best = h; }
    }
    return best < 0 ? -1 : face_of[best];
}

/* 输入环 -> 排列面 -> 奇偶保留 -> 简单子环。
 * 返回子环数；失败返回负错误码，at 写涉事输入边号。
 * 未自交的输入原样返回（单个环，顶点与输入完全一致）。 */
static int arrange_split(const WPoint *win, int n, WRing *out, int cap,
                         int *at, int *at2) {
    double tol = abs_tol(win, n);
    (void)at2;

    /* 收集全部真穿过 */
    WCross cross[WN_MAX_CROSS];
    int ncross = 0;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            if (adjacent_edges(i, j, n)) continue;
            WPoint a = win[i], b = win[(i + 1) % n];
            WPoint c = win[j], d = win[(j + 1) % n];
            if (classify(a, b, c, d, tol) != WN_TOUCH_PROPER) continue;
            if (ncross >= WN_MAX_CROSS) {
                *at = i;
                return WN_ERR_CAP;
            }
            double rx = b.x - a.x, ry = b.y - a.y;
            double sx = d.x - c.x, sy = d.y - c.y;
            double den = rx * sy - ry * sx;
            double qx = c.x - a.x, qy = c.y - a.y;
            cross[ncross].i = i;
            cross[ncross].j = j;
            cross[ncross].t = (qx * sy - qy * sx) / den;
            cross[ncross].u = (qx * ry - qy * rx) / den;
            cross[ncross].vid = -1;
            ++ncross;
        }
    if (ncross == 0) {
        if (cap < 1) return WN_ERR_TOO_MANY_RINGS;
        out[0].n = n;
        memcpy(out[0].pts, win, sizeof(WPoint) * (size_t)n);
        return 1;
    }

    /* 顶点：前 n 个是输入顶点，交点追加在后 */
    WPoint verts[WN_MAX_ARR_VERTS];
    for (int i = 0; i < n; ++i) verts[i] = win[i];
    int nv = n;
    for (int c = 0; c < ncross; ++c) {
        WPoint a = win[cross[c].i], b = win[(cross[c].i + 1) % n];
        WPoint q;
        q.x = a.x + (b.x - a.x) * cross[c].t;
        q.y = a.y + (b.y - a.y) * cross[c].t;
        int vid = vid_at(verts, &nv, q, tol * 2.0);
        if (vid < 0) { *at = cross[c].i; return WN_ERR_CAP; }
        cross[c].vid = vid;
    }

    /* 生成互不相交的线段（半边成对入栈） */
    WHalf half[WN_MAX_ARR_HALF];
    int nh = 0;
    for (int i = 0; i < n; ++i) {
        WSplit sp[WN_MAX_ARR_SPLIT];
        int ns = 0;
        sp[ns].t = 0.0; sp[ns].vid = i; ++ns;
        sp[ns].t = 1.0; sp[ns].vid = (i + 1) % n; ++ns;
        for (int c = 0; c < ncross; ++c) {
            if (cross[c].i == i) {
                if (ns >= WN_MAX_ARR_SPLIT) { *at = i; return WN_ERR_CAP; }
                sp[ns].t = cross[c].t; sp[ns].vid = cross[c].vid; ++ns;
            } else if (cross[c].j == i) {
                if (ns >= WN_MAX_ARR_SPLIT) { *at = i; return WN_ERR_CAP; }
                sp[ns].t = cross[c].u; sp[ns].vid = cross[c].vid; ++ns;
            }
        }
        for (int a = 0; a < ns; ++a)
            for (int b = a + 1; b < ns; ++b)
                if (sp[b].t < sp[a].t) {
                    WSplit tt = sp[a]; sp[a] = sp[b]; sp[b] = tt;
                }
        for (int a = 0; a + 1 < ns; ++a) {
            int u = sp[a].vid, v = sp[a + 1].vid;
            if (u == v) continue;
            if (nh + 2 > WN_MAX_ARR_HALF) { *at = i; return WN_ERR_CAP; }
            half[nh].u = u; half[nh].v = v; half[nh].src_edge = i; half[nh].fwd = 1;
            half[nh + 1].u = v; half[nh + 1].v = u; half[nh + 1].src_edge = i;
            half[nh + 1].fwd = 0;
            nh += 2;
        }
    }
    if (nh < 6) return WN_ERR_DEGENERATE;

    /* 每个顶点出发的半边按极角排序（CSR 平铺，避免二维大数组） */
    int cnt[WN_MAX_ARR_VERTS + 1];
    for (int v = 0; v <= nv; ++v) cnt[v] = 0;
    for (int h = 0; h < nh; ++h) ++cnt[half[h].u];
    int off[WN_MAX_ARR_VERTS + 1];
    off[0] = 0;
    for (int v = 0; v < nv; ++v) off[v + 1] = off[v] + cnt[v];
    int adj[WN_MAX_ARR_HALF];
    int fill[WN_MAX_ARR_VERTS];
    for (int v = 0; v < nv; ++v) fill[v] = off[v];
    for (int h = 0; h < nh; ++h) adj[fill[half[h].u]++] = h;
    for (int v = 0; v < nv; ++v) {
        int lo = off[v], hi = off[v + 1];
        for (int a = lo; a < hi; ++a)
            for (int b = a + 1; b < hi; ++b) {
                double aa = half_angle(verts, half[adj[a]]);
                double bb = half_angle(verts, half[adj[b]]);
                int swap;
                if (bb < aa) swap = 1;
                else if (bb > aa) swap = 0;
                else swap = adj[b] < adj[a];   /* 同角度：稳定次序 */
                if (swap) {
                    int t = adj[a]; adj[a] = adj[b]; adj[b] = t;
                }
            }
    }

    int face_of[WN_MAX_ARR_HALF];
    int face_start[WN_MAX_ARR_HALF];   /* 每个面的一条代表半边 */
    int face_n = 0;
    for (int h = 0; h < nh; ++h) face_of[h] = -1;
    for (int start = 0; start < nh; ++start) {
        if (face_of[start] >= 0) continue;
        int h = start;
        int guard = 0;
        while (face_of[h] < 0) {
            if (++guard > nh) { *at = half[start].src_edge; return WN_ERR_SELF_CROSS; }
            face_of[h] = face_n;
            int v = half[h].v, t = h ^ 1;   /* 每对半边相邻入栈，twin = h^1 */
            int pos = -1;
            for (int a = off[v]; a < off[v + 1]; ++a)
                if (adj[a] == t) { pos = a; break; }
            if (pos < 0) { *at = half[h].src_edge; return WN_ERR_SELF_CROSS; }
            int lo = off[v], len = off[v + 1] - off[v];
            h = adj[lo + ((pos - lo) + len - 1) % len];
        }
        if (face_n >= WN_MAX_ARR_HALF) { *at = 0; return WN_ERR_CAP; }
        face_start[face_n] = start;
        ++face_n;
    }

    /* 精确环绕数传播：无界面环绕数为 0；跨过半边 h 到其对侧时，若 h 与
     * 输入方向一致则减 1，否则加 1（等价于"输入方向的左侧环绕数多 1"）。
     * 用 BFS 从无界面传遍所有面，避免任何试探点或容差判断。 */
    int wnd_of[WN_MAX_ARR_HALF];
    int seen[WN_MAX_ARR_HALF];
    for (int f = 0; f < face_n; ++f) { wnd_of[f] = 0; seen[f] = 0; }
    int seed = outer_seed_face(verts, half, nh, face_of, nv);
    if (seed < 0) { *at = 0; return WN_ERR_SELF_CROSS; }
    int queue[WN_MAX_ARR_HALF];
    int qh = 0, qt = 0;
    wnd_of[seed] = 0;
    seen[seed] = 1;
    queue[qt++] = seed;
    while (qh < qt) {
        int f = queue[qh++];
        for (int h = 0; h < nh; ++h) {
            if (face_of[h] != f) continue;
            int g = face_of[h ^ 1];
            if (seen[g]) continue;
            wnd_of[g] = wnd_of[f] + (half[h].fwd ? -1 : 1);
            seen[g] = 1;
            if (qt >= WN_MAX_ARR_HALF) { *at = half[h].src_edge; return WN_ERR_CAP; }
            queue[qt++] = g;
        }
    }
    for (int f = 0; f < face_n; ++f)
        if (!seen[f]) { *at = 0; return WN_ERR_SELF_CROSS; }  /* 图不连通 */

    int nring = 0;
    for (int f = 0; f < face_n; ++f) {
        if ((wnd_of[f] & 1) == 0) continue;   /* 奇偶规则下不填充的面 */
        /* 收集该面的边界顶点（面在每条半边的左侧）。 */
        WPoint seq[WN_MAX_ARR_VERTS];
        int ns = 0;
        int h = face_start[f];
        int guard = 0;
        do {
            if (ns >= WN_MAX_ARR_VERTS) { *at = half[h].src_edge; return WN_ERR_CAP; }
            seq[ns++] = verts[half[h].u];
            int v = half[h].v, t = h ^ 1;
            int pos = -1;
            for (int a = off[v]; a < off[v + 1]; ++a)
                if (adj[a] == t) { pos = a; break; }
            int lo = off[v], len = off[v + 1] - off[v];
            h = adj[lo + ((pos - lo) + len - 1) % len];
            if (++guard > nh) { *at = half[face_start[f]].src_edge; return WN_ERR_SELF_CROSS; }
        } while (h != face_start[f]);
        if (ns < 3) continue;
        double ar = winnorm_signed_area(seq, ns);
        /* 面的几何走向由半边结构决定（有界面恒为逆时针）；子环面积符号要
         * 忠实于输入在该处的绕向：环绕数为负就整环反向。 */
        if ((ar > 0.0) != (wnd_of[f] > 0)) {
            WRing tmp;
            tmp.n = ns;
            memcpy(tmp.pts, seq, sizeof(WPoint) * (size_t)ns);
            if (nring >= cap) return WN_ERR_TOO_MANY_RINGS;
            reverse_ring(&tmp, &out[nring]);
            out[nring].n = ns;
            ++nring;
            continue;
        }
        if (nring >= cap) return WN_ERR_TOO_MANY_RINGS;
        out[nring].n = ns;
        memcpy(out[nring].pts, seq, sizeof(WPoint) * (size_t)ns);
        ++nring;
    }
    if (nring == 0) {
        *at = 0;
        return WN_ERR_DEGENERATE;
    }
    return nring;
}

int winnorm_split_self(const WPoint *win, int n, WRing rings[], int cap,
                       int *at, int *at2) {
    if (at) *at = -1;
    if (at2) *at2 = -1;
    if (!win || !rings) return WN_ERR_NULL;
    if (n < 3) return WN_ERR_TOO_FEW;
    if (n > WN_MAX_VERTS) return WN_ERR_TOO_MANY;
    if (cap <= 0) return WN_ERR_CAP;
    double tol = abs_tol(win, n);

    int dup = first_dup(win, n, tol);
    if (dup >= 0) {
        if (at) *at = dup;
        return WN_ERR_DUP_POINT;
    }
    /* 顶点贴边/共线重叠不是"真穿过"，排列也消不掉：先拒并给边号。 */
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            if (adjacent_edges(i, j, n)) continue;
            int c = classify(win[i], win[(i + 1) % n],
                             win[j], win[(j + 1) % n], tol);
            if (c == WN_TOUCH_NONE || c == WN_TOUCH_PROPER) continue;
            if (at) *at = i;
            if (at2) *at2 = j;
            return WN_ERR_CONTACT;
        }

    return arrange_split(win, n, rings, cap, at, at2);
}

/* 只逆序、不改顶点集合；首点保持不动，方便与输入逐点对照。 */
static void reverse_ring(const WRing *src, WRing *dst) {
    int n = src->n;
    dst->n = n;
    dst->pts[0] = src->pts[0];
    for (int i = 1; i < n; ++i) dst->pts[i] = src->pts[n - i];
}

/* 拆分/校验之后，把一组简单子环逐个定向并统一成逆时针。
 * abs_before 用精确求和复算每个子环，写进 abs_area 与 in_signed_area。 */
static int orient_rings(const WRing *sub, int nr, WNorm *out, int *at) {
    out->rings = 0;
    for (int k = 0; k < nr; ++k) {
        double tol = abs_tol(sub[k].pts, sub[k].n);
        double s = winnorm_signed_area_exact(sub[k].pts, sub[k].n);
        double absv = s < 0.0 ? -s : s;
        /* 退化：面积与容差同量级。容差按坐标量级放大，避免大坐标下
         * 把正常的细小环误判成零面积。 */
        if (!(absv > tol * tol)) {
            if (at) *at = k;
            out->rings = 0;
            return WN_ERR_DEGENERATE;
        }
        out->abs_area[k] = absv;
        out->in_signed_area[k] = s;
        if (s > 0.0) {
            out->ring[k] = sub[k];
            out->flipped[k] = 0;
        } else {
            reverse_ring(&sub[k], &out->ring[k]);
            out->flipped[k] = 1;
            /* 不变量守护：翻转后用同一套求和复算，|面积| 必须逐位相同。 */
            double r = winnorm_signed_area_exact(out->ring[k].pts, out->ring[k].n);
            double rabs = r < 0.0 ? -r : r;
            if (memcmp(&rabs, &absv, sizeof absv) != 0) {
                if (at) *at = k;
                out->rings = 0;
                return WN_ERR_AREA_DRIFT;
            }
        }
    }
    out->rings = nr;
    return WN_OK;
}

int winnorm_normalize_ring(const WPoint *win, int n, WRing *ring,
                           double *abs_area, int *flipped,
                           int *at, int *at2) {
    if (at) *at = -1;
    if (at2) *at2 = -1;
    if (!win || !ring) return WN_ERR_NULL;
    int code = WN_OK, a = -1, b = -1;
    int chk = winnorm_check(win, n, &code, &a, &b);
    if (chk != 0) {
        if (at) *at = a;
        if (at2) *at2 = b;
        return code;
    }
    WNorm nrm;
    int rc = winnorm_normalize(win, n, 0, &nrm, &a, &b);
    if (rc != WN_OK) {
        if (at) *at = a;
        if (at2) *at2 = b;
        return rc;
    }
    if (nrm.rings != 1) {
        if (at) *at = 1;
        return WN_ERR_SELF_CROSS;
    }
    *ring = nrm.ring[0];
    if (abs_area) *abs_area = nrm.abs_area[0];
    if (flipped) *flipped = nrm.flipped[0];
    return WN_OK;
}

int winnorm_normalize_set(const WRing *rings, int nr, WNorm *out,
                          int *at, int *at2) {
    if (at) *at = -1;
    if (at2) *at2 = -1;
    if (!rings || !out) return WN_ERR_NULL;
    if (nr <= 0) return WN_ERR_TOO_FEW;
    if (nr > WN_MAX_RINGS) return WN_ERR_TOO_MANY_RINGS;
    for (int k = 0; k < nr; ++k) {
        int code = WN_OK, a = -1, b = -1;
        int chk = winnorm_check(rings[k].pts, rings[k].n, &code, &a, &b);
        if (chk != 0 || code == WN_ERR_SELF_CROSS) {
            if (at) *at = a;
            if (at2) *at2 = b;
            return chk == 0 ? WN_ERR_SELF_CROSS : code;
        }
    }
    int a = -1;
    int rc = orient_rings(rings, nr, out, &a);
    if (rc != WN_OK && at) *at = a;
    return rc;
}

int winnorm_normalize(const WPoint *win, int n, int allow_split, WNorm *out,
                      int *at, int *at2) {
    if (at) *at = -1;
    if (at2) *at2 = -1;
    if (!win || !out) return WN_ERR_NULL;
    out->rings = 0;

    if (!allow_split) {
        int code = WN_OK, a = -1, b = -1;
        int chk = winnorm_check(win, n, &code, &a, &b);
        if (chk != 0) {
            if (at) *at = a;
            if (at2) *at2 = b;
            return code;
        }
        WRing one;
        one.n = n;
        memcpy(one.pts, win, sizeof(WPoint) * (size_t)n);
        int a2 = -1;
        int rc = orient_rings(&one, 1, out, &a2);
        if (rc != WN_OK && at) *at = a2;
        return rc;
    }

    WRing sub[WN_MAX_RINGS];
    int a = -1, b = -1;
    int nr = winnorm_split_self(win, n, sub, WN_MAX_RINGS, &a, &b);
    if (nr < 0) {
        if (at) *at = a;
        if (at2) *at2 = b;
        return nr;
    }
    int a2 = -1;
    int rc = orient_rings(sub, nr, out, &a2);
    if (rc != WN_OK && at) *at = a2;
    return rc;
}

const char *winnorm_strerror(int code) {
    switch (code) {
    case WN_OK:                  return "ok";
    case WN_ERR_NULL:            return "拒绝：空指针";
    case WN_ERR_TOO_FEW:         return "拒绝：顶点不足三个";
    case WN_ERR_TOO_MANY:        return "拒绝：顶点超过上限";
    case WN_ERR_DUP_POINT:       return "拒绝：存在重复点";
    case WN_ERR_SELF_CROSS:      return "拒绝：自交未拆分";
    case WN_ERR_CONTACT:         return "拒绝：残留顶点接触或共线重叠";
    case WN_ERR_DEGENERATE:      return "拒绝：子环面积为零（退化）";
    case WN_ERR_TOO_MANY_RINGS:  return "拒绝：子环数超过上限";
    case WN_ERR_CAP:             return "拒绝：子环顶点数超过容量";
    case WN_ERR_AREA_DRIFT:      return "拒绝：翻转前后面积绝对值不再逐位相同";
    default:                     return "拒绝：未知错误";
    }
}
