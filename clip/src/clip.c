#include "clip.h"
#include <stdlib.h>

/* Sutherland-Hodgman 凸多边形裁剪。
 * 返回值：
 *    0  成功（out->count 可能为 0，表示完全在窗口外）
 *   -1  参数为空或顶点数不足（多边形/窗口少于 3 个顶点）
 *   -2  窗口退化（相邻顶点重合，无法定义裁剪半平面）
 *   -3  内存不足，或结果超出调用方 CPoly 的 64 点容量
 *
 * 每一步裁剪只按“当前顶点数 + 1”分配紧凑缓冲区，
 * 单步工作内存上限由 clip_working_bytes() 给出，便于上层按
 * 输入规模（多边形 + 窗口顶点数）做四倍预算检查。
 */

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

/* 有符号面积的两倍；正为逆时针，负为顺时针。 */
static double signed_area2(const CPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        CPoint a = p[i], b = p[(i + 1) % n];
        s += a.x * b.y - b.x * a.y;
    }
    return s;
}

/* 单步裁剪实际需要的工作内存（字节）：cur/nxt 两个紧凑点数组，
 * 每步输出至多比输入多一个点，容量按 n+1 分配。 */
size_t clip_working_bytes(int n) {
    if (n < 3) return 0;
    return 2 * (size_t)(n + 1) * sizeof(CPoint);
}

int clip_convex(const CPoint *poly, int n, const CPoint *win, int wn, CPoly *out) {
    if (!poly || !win || !out || n < 3 || wn < 3) return -1;

    /* 窗口方向归一化：Sutherland-Hodgman 要求窗口顶点逆时针
     * （每边左侧为保留侧）。顺时针窗口直接逐边裁会裁掉内部，
     * 这正是“裁一刀”和“裁两刀”对不上的来源之一。 */
    CPoint *w = (CPoint *)malloc((size_t)wn * sizeof(CPoint));
    if (!w) return -3;
    if (signed_area2(win, wn) < 0.0) {
        for (int i = 0; i < wn; ++i) w[i] = win[wn - 1 - i];
    } else {
        for (int i = 0; i < wn; ++i) w[i] = win[i];
    }
    for (int i = 0; i < wn; ++i) {
        CPoint a = w[i], b = w[(i + 1) % wn];
        if (a.x == b.x && a.y == b.y) {
            free(w);
            return -2;
        }
    }

    CPoint *cur = (CPoint *)malloc((size_t)(n + 1) * sizeof(CPoint));
    CPoint *nxt = (CPoint *)malloc((size_t)(n + 1) * sizeof(CPoint));
    if (!cur || !nxt) {
        free(w);
        free(cur);
        free(nxt);
        return -3;
    }
    int cn = n;
    for (int i = 0; i < n; ++i) cur[i] = poly[i];

    for (int e = 0; e < wn; ++e) {
        CPoint a = w[e], b = w[(e + 1) % wn];
        int nn = 0;
        for (int i = 0; i < cn; ++i) {
            CPoint p = cur[i], q = cur[(i + 1) % cn];
            int ip = inside(a, b, p), iq = inside(a, b, q);
            if (ip) {
                if (nn >= n + 1) { free(w); free(cur); free(nxt); return -3; }
                nxt[nn++] = p;
            }
            if (ip != iq) {
                CPoint m = meet(p, q, a, b);
                if (nn >= n + 1) { free(w); free(cur); free(nxt); return -3; }
                nxt[nn++] = m;
            }
        }
        CPoint *t = cur; cur = nxt; nxt = t;
        cn = nn;
        if (cn < 3) break;
    }
    free(w);
    free(nxt);

    if (cn > 64) {
        free(cur);
        return -3;
    }
    out->count = cn;
    for (int i = 0; i < cn; ++i) out->pts[i] = cur[i];
    free(cur);
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
