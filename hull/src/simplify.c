#include "simplify.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define MAXN 64

static double cross3(HPoint o, HPoint a, HPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

/* 有向鞋带面积的两倍（逆时针为正） */
static double area2(const HPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const HPoint *a = &p[i], *b = &p[(i + 1) % n];
        s += a->x * b->y - a->y * b->x;
    }
    return s;
}

int hull_estimate_keep(int n0, double ratio) {
    if (n0 < 3 || !(ratio > 0.0) || ratio > 1.0) return -1;
    if (n0 == 3 || ratio >= 1.0) return n0;
    /* 正 n 边形面积 A(n) = n/2 * R^2 * sin(2pi/n)，
     * 小角度展开 sin t = t - t^3/6 + ...：
     *   A(n)/(pi R^2) = 1 - (2 pi^2)/(3 n^2) + O(n^-4)
     * 令面积比 = ratio，反解 n = pi * sqrt(2 / (3(1-ratio))) */
    double d = 1.0 - ratio;
    if (d < 1e-12) return n0;
    double n = M_PI * sqrt(2.0 / (3.0 * d));
    int k = (int)ceil(n - 1e-9);
    if (k < 3) k = 3;
    if (k > n0) k = n0;
    return k;
}

/* 共线点剔除：返回去重后点数；落在任意两点连线段上的点计入 *ncoll。
 * 严格凸包顶点不可能落在别的两点线段上，故被剔除的只有共线边界点。 */
static int drop_collinear(const HPoint *in, int n, HPoint *out, int *ncoll) {
    int m = 0, coll = 0;
    for (int i = 0; i < n; ++i) {
        int dup = 0;
        for (int j = 0; j < m; ++j) {
            double dx = in[i].x - out[j].x, dy = in[i].y - out[j].y;
            if (dx * dx + dy * dy <= 1e-18) { dup = 1; break; }
        }
        if (dup) { ++coll; continue; }   /* 重合点按共线退化一并计数 */
        out[m++] = in[i];
    }
    HPoint keep[MAXN];
    int km = 0;
    for (int i = 0; i < m; ++i) {
        int on_line = 0;
        for (int a = 0; a < m && !on_line; ++a) {
            if (a == i) continue;
            for (int b = a + 1; b < m && !on_line; ++b) {
                if (b == i) continue;
                HPoint pa = out[a], pb = out[b], pi = out[i];
                double cr = cross3(pa, pb, pi);
                double d2 = (pb.x - pa.x) * (pb.x - pa.x) +
                            (pb.y - pa.y) * (pb.y - pa.y);
                double tol = 1e-12 * (1.0 + sqrt(d2));
                if (fabs(cr) > tol) continue;
                /* 共线后还要落在线段 a-b 内部（含端点已被去重排除） */
                double dot = (pi.x - pa.x) * (pb.x - pa.x) +
                             (pi.y - pa.y) * (pb.y - pa.y);
                if (dot > 0.0 && dot < d2) on_line = 1;
            }
        }
        if (on_line) ++coll;
        else keep[km++] = out[i];
    }
    memcpy(out, keep, sizeof(HPoint) * (size_t)km);
    *ncoll = coll;
    return km;
}

int hull_simplify(const HPoint *pts, int n, double ratio, HSimp *out) {
    if (!pts || !out) return -1;
    if (n < 1 || n > MAXN) return -1;
    if (!(ratio > 0.0) || ratio > 1.0) return -2;
    memset(out, 0, sizeof(*out));
    out->raw_count = n;
    out->target = ratio;

    if (n < 3) return -3;

    HPoint core[MAXN];
    int m = drop_collinear(pts, n, core, &out->collinear);
    if (m < 3) return -4;

    HPoly hull;
    if (hull_convex(core, m, &hull) != 0 || hull.count < 3) return -4;
    int hn = hull.count;
    out->hull_count = hn;
    out->area_full = fabs(area2(hull.pts, hn)) * 0.5;

    int est = hull_estimate_keep(hn, ratio);
    out->est_count = est;

    /* 贪心逐点剔除：每步删除"耳朵"三角形面积最小的顶点，
     * 删除前验证实际多边形面积不会跌破 ratio * A0。 */
    HPoint p[MAXN];
    int nxt[MAXN], prv[MAXN], alive = hn;
    for (int i = 0; i < hn; ++i) {
        p[i] = hull.pts[i];
        nxt[i] = (i + 1) % hn;
        prv[i] = (i + hn - 1) % hn;
    }
    double floor_area = ratio * out->area_full;
    double cur_area = out->area_full;

    while (alive > 3) {
        int best = -1;
        double best_ear = 0.0;
        for (int i = 0; i < hn; ++i) {
            if (nxt[i] < 0) continue;
            double ear = fabs(cross3(p[prv[i]], p[i], p[nxt[i]])) * 0.5;
            if (best < 0 || ear < best_ear) { best_ear = ear; best = i; }
        }
        double try_area = cur_area - best_ear;
        if (try_area + 1e-12 < floor_area) break;   /* 再剔就破线，停止 */
        int a = prv[best], b = nxt[best];
        nxt[a] = b; prv[b] = a; nxt[best] = prv[best] = -1;
        --alive;
        cur_area = try_area;
    }

    if (alive < 3) return -4;

    int k = 0;
    for (int i = 0; i < hn && k < alive; ++i)
        if (nxt[i] >= 0) out->poly.pts[k++] = p[i];
    out->poly.count = alive;
    out->kept = alive;
    out->area_kept = fabs(area2(out->poly.pts, alive)) * 0.5;
    out->ratio = out->area_kept / out->area_full;
    if (out->ratio + 1e-12 < ratio) return -4;   /* 兜底：不允许破线 */
    return 0;
}
