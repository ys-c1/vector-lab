#include "stroke.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* 内部容量：闭环序列长度 = 原始点数 + 2×交点数。 */
#define STROKE_MAX_NODES (STROKE_MAX_RAW + STROKE_MAX_HITS)
#define STROKE_MAX_WALK  (STROKE_MAX_RAW + 2 * STROKE_MAX_HITS)

typedef struct {
    double x, y;
    int    edge;    /* 该节点所在的原始边号（交点取两条原边中较小者） */
} SNode;

typedef struct {
    int    edge;
    double t;
    int    node;
} SEvent;

static double cross2(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static double signed_area(const SPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        s += p[i].x * p[j].y - p[j].x * p[i].y;
    }
    return s * 0.5;
}

static double dist2(SPoint a, SPoint b) {
    double dx = a.x - b.x, dy = a.y - b.y;
    return dx * dx + dy * dy;
}

/* 求两条线段的交。
 * 返回 STROKE_HIT_*；ta/tb 为参数，at 为交点（共线时取重叠起点）。
 * 平行不共线或不相交时返回 0。 */
static int seg_hit(SPoint a0, SPoint a1, SPoint b0, SPoint b1,
                   double *ta, double *tb, SPoint *at) {
    double rx = a1.x - a0.x, ry = a1.y - a0.y;
    double sx = b1.x - b0.x, sy = b1.y - b0.y;
    double qx = b0.x - a0.x, qy = b0.y - a0.y;
    double lr = hypot(rx, ry), ls = hypot(sx, sy);
    double scale = 1.0;
    double mags[8] = { fabs(a0.x), fabs(a0.y), fabs(a1.x), fabs(a1.y),
                       fabs(b0.x), fabs(b0.y), fabs(b1.x), fabs(b1.y) };
    for (int k = 0; k < 8; ++k) if (mags[k] > scale) scale = mags[k];

    if (lr < 1e-12 || ls < 1e-12) return 0;

    double den = cross2(rx, ry, sx, sy);
    double tol = 1e-9 * scale;

    if (fabs(den) <= 1e-12 * lr * ls) {
        /* 平行：先看是否共线 */
        if (fabs(cross2(qx, qy, rx, ry)) > tol * lr) return 0;
        double rr = lr * lr;
        double u0 = (qx * rx + qy * ry) / rr;
        double u1 = ((b1.x - a0.x) * rx + (b1.y - a0.y) * ry) / rr;
        double lo = u0 < u1 ? u0 : u1;
        double hi = u0 < u1 ? u1 : u0;
        double olo = lo > 0.0 ? lo : 0.0;
        double ohi = hi < 1.0 ? hi : 1.0;
        if (ohi < olo - 1e-9) return 0;
        *ta = olo;
        *tb = 0.0;
        at->x = a0.x + rx * olo;
        at->y = a0.y + ry * olo;
        double ov = (ohi - olo) * lr;              /* 重叠长度 */
        if (ov <= tol) return STROKE_HIT_ENDPOINT;  /* 只碰到一点 */
        return STROKE_HIT_COLLINEAR;
    }

    double t = cross2(qx, qy, sx, sy) / den;
    double u = cross2(qx, qy, rx, ry) / den;
    if (t < -1e-9 || t > 1.0 + 1e-9 || u < -1e-9 || u > 1.0 + 1e-9) return 0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    if (u < 0.0) u = 0.0;
    if (u > 1.0) u = 1.0;
    *ta = t;
    *tb = u;
    at->x = a0.x + rx * t;
    at->y = a0.y + ry * t;

    /* 端点判定用几何距离，避免长边短参数误判。 */
    SPoint e[4] = { a0, a1, b0, b1 };
    for (int k = 0; k < 4; ++k)
        if (dist2(*at, e[k]) <= tol * tol) return STROKE_HIT_ENDPOINT;
    return STROKE_HIT_PROPER;
}

int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out) {
    if (!pts || !out || n < 2 || width <= 0.0) return -1;
    double h = width * 0.5;
    int k = 0;
    for (int i = 0; i < n && k < 126; ++i) {
        double dx = 0.0, dy = 0.0;
        if (i + 1 < n) { dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y; }
        else { dx = pts[i].x - pts[i-1].x; dy = pts[i].y - pts[i-1].y; }
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) return -2;
        double nx = -dy / len * h, ny = dx / len * h;
        out->pts[k].x = pts[i].x + nx; out->pts[k].y = pts[i].y + ny; ++k;
        out->pts[k].x = pts[i].x - nx; out->pts[k].y = pts[i].y - ny; ++k;
    }
    out->count = k;
    return 0;
}

static int cmp_event(const void *pa, const void *pb) {
    const SEvent *a = pa, *b = pb;
    if (a->edge != b->edge) return a->edge < b->edge ? -1 : 1;
    if (a->t < b->t) return -1;
    if (a->t > b->t) return 1;
    return 0;
}

/* 把闭环序列按栈式方法拆成简单环。
 * 每遇到重复节点，就把栈顶到该节点的一段弹成一个环；最后剩下的栈是主环。
 * 每弹出一个环就立即把顶点拷进 out->points/edges，避免后续覆盖栈内容。 */
static int decompose_walk(SStrokeResult *out, const SNode *nodes,
                          const int *walk, int m, const int *hit_node) {
    int pos[STROKE_MAX_NODES];
    int stack[STROKE_MAX_WALK];
    int ring_off[STROKE_MAX_LOOPS];
    int ring_len[STROKE_MAX_LOOPS];
    int ring_node_off[STROKE_MAX_LOOPS];
    int ring_area_sign[STROKE_MAX_LOOPS];
    double ring_area[STROKE_MAX_LOOPS];
    /* 各环的节点号依次写入 node_buf；所有环合计恰好覆盖整条闭环序列，
     * 因此总长不超过 m。 */
    int node_buf[STROKE_MAX_WALK];
    int node_used = 0;
    int ring_count = 0;
    int sp = 0;

    for (int i = 0; i < STROKE_MAX_NODES; ++i) pos[i] = -1;

    for (int i = 0; i < m; ++i) {
        int v = walk[i];
        if (pos[v] >= 0) {
            int k = pos[v];
            int len = sp - k;
            if (len < 3) return STROKE_E_INTERNAL;
            if (ring_count >= STROKE_MAX_LOOPS) return STROKE_E_CAP;
            if (out->point_count + len > STROKE_POOL_PTS) return STROKE_E_CAP;
            if (node_used + len > STROKE_MAX_WALK) return STROKE_E_CAP;
            int off = out->point_count;
            for (int q = 0; q < len; ++q) {
                int node = stack[k + q];
                out->points[off + q].x = nodes[node].x;
                out->points[off + q].y = nodes[node].y;
                out->edges[off + q] = nodes[node].edge;
                node_buf[node_used + q] = node;
            }
            out->point_count += len;
            double ar = signed_area(out->points + off, len);
            ring_off[ring_count] = off;
            ring_len[ring_count] = len;
            ring_node_off[ring_count] = node_used;
            node_used += len;
            ring_area[ring_count] = ar;
            ring_area_sign[ring_count] = ar > 0.0 ? 1 : (ar < 0.0 ? -1 : 0);
            ++ring_count;
            for (int q = k; q < sp; ++q) pos[stack[q]] = -1;
            sp = k;
        }
        pos[v] = sp;
        stack[sp++] = v;
    }
    if (sp >= 3) {
        if (ring_count >= STROKE_MAX_LOOPS) return STROKE_E_CAP;
        if (out->point_count + sp > STROKE_POOL_PTS) return STROKE_E_CAP;
        if (node_used + sp > STROKE_MAX_WALK) return STROKE_E_CAP;
        int off = out->point_count;
        for (int q = 0; q < sp; ++q) {
            int node = stack[q];
            out->points[off + q].x = nodes[node].x;
            out->points[off + q].y = nodes[node].y;
            out->edges[off + q] = nodes[node].edge;
            node_buf[node_used + q] = node;
        }
        out->point_count += sp;
        double ar = signed_area(out->points + off, sp);
        ring_off[ring_count] = off;
        ring_len[ring_count] = sp;
        ring_node_off[ring_count] = node_used;
        node_used += sp;
        ring_area[ring_count] = ar;
        ring_area_sign[ring_count] = ar > 0.0 ? 1 : (ar < 0.0 ? -1 : 0);
        ++ring_count;
    }
    if (ring_count == 0) return STROKE_E_INTERNAL;

    /* 参照方向取原始轮廓的有向面积；退化为零面积时取主环方向。 */
    int main_sign = out->raw_area > 0.0 ? 1 : (out->raw_area < 0.0 ? -1 : 0);
    if (main_sign == 0) main_sign = ring_area_sign[ring_count - 1];
    if (main_sign == 0) main_sign = 1;
    out->total_loop_count = ring_count;

    for (int r = 0; r < ring_count; ++r) {
        int off = ring_off[r];
        int len = ring_len[r];
        int sign = ring_area_sign[r];
        double ar = ring_area[r];

        /* 环上关联的交点对号（升序）。 */
        int pairs[STROKE_MAX_PAIRS];
        int npairs = 0, ntotal = 0;
        for (int h = 0; h < out->hit_count; ++h) {
            int node = hit_node[h];
            int on = 0;
            for (int q = 0; q < len; ++q)
                if (node_buf[ring_node_off[r] + q] == node) { on = 1; break; }
            if (!on) continue;
            int dup = 0;
            for (int q = 0; q < npairs; ++q)
                if (pairs[q] == out->hits[h].pair) { dup = 1; break; }
            if (dup) continue;
            ++ntotal;
            if (npairs < STROKE_MAX_PAIRS) pairs[npairs++] = out->hits[h].pair;
        }
        for (int a = 0; a < npairs; ++a)
            for (int b = a + 1; b < npairs; ++b)
                if (pairs[b] < pairs[a]) { int t = pairs[a]; pairs[a] = pairs[b]; pairs[b] = t; }

        /* 归属只看有向面积符号：闭环分解满足 Σ 各环有向面积 = 原始面积，
         * 与主环同向的环保留，反向绕入的环裁掉并编号。 */
        int keep = (sign == main_sign);

        if (keep) {
            SStrokeRing *ring = &out->kept[out->kept_count++];
            ring->loop_id = r;
            ring->count = len;
            ring->off = off;
            ring->area = ar;
            ring->first_pair = npairs ? pairs[0] : -1;
            out->kept_area += ar;
        } else if (sign == 0) {
            /* 零面积环既不是保留环也不是可编号的内翻环。 */
            continue;
        } else {
            SStrokeLoop *lp = &out->loops[out->loop_count];
            lp->index = out->loop_count + 1;
            lp->loop_id = r;
            lp->count = len;
            lp->off = off;
            lp->area = ar;
            lp->pair = npairs ? pairs[0] : -1;
            lp->ea = lp->eb = -1;
            lp->at.x = lp->at.y = 0.0;
            if (lp->pair >= 0) {
                SStrokeHit *hh = &out->hits[lp->pair];
                lp->ea = hh->ea;
                lp->eb = hh->eb;
                lp->at = hh->at;
            }
            lp->hit_pair_count = npairs;
            lp->hit_pair_total = ntotal;
            for (int q = 0; q < npairs; ++q) lp->hit_pairs[q] = pairs[q];
            out->loop_count++;
            out->removed_area += ar;
        }
    }

    out->closure_error = out->raw_area - out->kept_area - out->removed_area;
    return STROKE_OK;
}

/* 不裁环 / 无自交：输出与输入逐点一致。 */
static void keep_raw_as_is(SStrokeResult *out) {
    out->point_count = out->raw_count;
    for (int i = 0; i < out->raw_count; ++i) {
        out->points[i] = out->raw[i];
        out->edges[i] = i;
    }
    out->kept_count = 1;
    out->kept[0].loop_id = 0;
    out->kept[0].count = out->raw_count;
    out->kept[0].off = 0;
    out->kept[0].area = out->raw_area;
    out->kept[0].first_pair = -1;
    out->kept_area = out->raw_area;
    out->removed_area = 0.0;
    out->loop_count = 0;
    out->total_loop_count = 1;
    out->closure_error = 0.0;
}

int stroke_resolve_polygon(const SPoint *poly, int n, int cut, SStrokeResult *out) {
    if (!poly || !out || n < 3 || n > STROKE_MAX_RAW) return STROKE_E_ARG;
    memset(out, 0, sizeof(*out));
    out->status = STROKE_OK;
    out->bad_pair = -1;
    out->raw_simple = 1;
    out->raw_count = n;
    for (int i = 0; i < n; ++i) out->raw[i] = poly[i];
    out->raw_area = signed_area(poly, n);

    for (int i = 0; i < n; ++i) {
        int j = (i + 1) % n;
        if (dist2(poly[i], poly[j]) < 1e-18) {
            out->status = STROKE_E_ZERO_EDGE;
            keep_raw_as_is(out);
            return out->status;
        }
    }

    /* 1. 按段两两求交（跳过相邻边与首尾闭合边）。 */
    int hit_count = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (j == i + 1 || (i == 0 && j == n - 1)) continue;
            double ta = 0.0, tb = 0.0;
            SPoint at = {0.0, 0.0};
            int kind = seg_hit(poly[i], poly[(i + 1) % n],
                               poly[j], poly[(j + 1) % n], &ta, &tb, &at);
            if (!kind) continue;
            if (hit_count >= STROKE_MAX_HITS) {
                out->status = STROKE_E_CAP;
                keep_raw_as_is(out);
                return out->status;
            }
            SStrokeHit *hh = &out->hits[hit_count];
            hh->pair = hit_count;
            hh->ea = i;
            hh->eb = j;
            hh->kind = kind;
            hh->ta = ta;
            hh->tb = tb;
            hh->at = at;
            ++hit_count;
            out->raw_simple = 0;
            if (kind != STROKE_HIT_PROPER && out->degenerate == 0) {
                out->degenerate = kind;
                out->bad_pair = hh->pair;
            }
        }
    }
    out->hit_count = hit_count;

    /* 端点相交或共线重叠：报错并给出对号；cut=0 时仍只按原样保留。 */
    if (out->degenerate) {
        out->status = out->degenerate == STROKE_HIT_ENDPOINT
                    ? STROKE_E_ENDPOINT : STROKE_E_COLLINEAR;
        keep_raw_as_is(out);
        return cut ? out->status : STROKE_OK;
    }

    if (!cut || hit_count == 0) {
        keep_raw_as_is(out);
        return out->status;
    }

    /* 2. 建节点：原始顶点 + 合并后的交点。 */
    SNode nodes[STROKE_MAX_NODES];
    for (int i = 0; i < n; ++i) {
        nodes[i].x = poly[i].x;
        nodes[i].y = poly[i].y;
        nodes[i].edge = i;
    }
    int node_count = n;

    double scale = 1.0;
    for (int i = 0; i < n; ++i) {
        if (fabs(poly[i].x) > scale) scale = fabs(poly[i].x);
        if (fabs(poly[i].y) > scale) scale = fabs(poly[i].y);
    }
    double merge_tol = 1e-8 * scale;

    int hit_node[STROKE_MAX_HITS];
    for (int h = 0; h < hit_count; ++h) {
        SStrokeHit *hh = &out->hits[h];
        int id = -1;
        for (int v = 0; v < node_count; ++v) {
            double dx = nodes[v].x - hh->at.x, dy = nodes[v].y - hh->at.y;
            if (dx * dx + dy * dy <= merge_tol * merge_tol) { id = v; break; }
        }
        if (id < 0) {
            if (node_count >= STROKE_MAX_NODES) {
                out->status = STROKE_E_CAP;
                keep_raw_as_is(out);
                return out->status;
            }
            id = node_count++;
            nodes[id].x = hh->at.x;
            nodes[id].y = hh->at.y;
            nodes[id].edge = hh->ea;
        }
        hit_node[h] = id;
    }

    /* 3. 每条边的交点事件，按边、参数排序。 */
    SEvent ev[STROKE_MAX_WALK];
    int ev_count = 0;
    for (int h = 0; h < hit_count; ++h) {
        SStrokeHit *hh = &out->hits[h];
        ev[ev_count].edge = hh->ea; ev[ev_count].t = hh->ta; ev[ev_count].node = hit_node[h]; ++ev_count;
        ev[ev_count].edge = hh->eb; ev[ev_count].t = hh->tb; ev[ev_count].node = hit_node[h]; ++ev_count;
    }
    qsort(ev, (size_t)ev_count, sizeof(ev[0]), cmp_event);

    /* 4. 构造闭环节点序列。 */
    int walk[STROKE_MAX_WALK];
    int walk_len = 0;
    int e = 0;
    for (int i = 0; i < n; ++i) {
        walk[walk_len++] = i;
        while (e < ev_count && ev[e].edge == i) {
            int v = ev[e].node;
            if (walk_len > 0 && walk[walk_len - 1] == v) { ++e; continue; }
            walk[walk_len++] = v;
            ++e;
        }
        if (walk_len >= STROKE_MAX_WALK) {
            out->status = STROKE_E_CAP;
            keep_raw_as_is(out);
            return out->status;
        }
    }

    out->point_count = 0;
    out->kept_count = 0;
    out->loop_count = 0;
    out->kept_area = 0.0;
    out->removed_area = 0.0;
    int rc = decompose_walk(out, nodes, walk, walk_len, hit_node);
    if (rc != STROKE_OK) {
        out->status = rc;
        keep_raw_as_is(out);
        return rc;
    }
    out->status = STROKE_OK;
    return STROKE_OK;
}

int stroke_resolve(const SPoint *pts, int n, double width, int cut,
                   SStrokeResult *out) {
    if (!pts || !out) return STROKE_E_ARG;
    SPoly sp;
    int rc = stroke_polyline(pts, n, width, &sp);
    if (rc == -1) return STROKE_E_ARG;
    if (rc == -2) return STROKE_E_ZERO_EDGE;
    if (rc != 0) return STROKE_E_INTERNAL;
    return stroke_resolve_polygon(sp.pts, sp.count, cut, out);
}

long stroke_hits_csv(const SStrokeResult *r, FILE *fp) {
    if (!r || !fp) return -1;
    long n = 0;
    n += fprintf(fp, "pair,edge_a,edge_b,kind,ta,tb,x,y\n");
    for (int i = 0; i < r->hit_count; ++i) {
        const SStrokeHit *h = &r->hits[i];
        n += fprintf(fp, "%d,%d,%d,%d,%.12g,%.12g,%.12g,%.12g\n",
                     h->pair, h->ea, h->eb, h->kind,
                     h->ta, h->tb, h->at.x, h->at.y);
    }
    return n;
}

long stroke_loops_csv(const SStrokeResult *r, FILE *fp) {
    if (!r || !fp) return -1;
    long n = 0;
    n += fprintf(fp, "index,loop_id,count,area,pair,edge_a,edge_b,x,y,"
                     "hit_pair_count,hit_pair_total,hit_pairs\n");
    for (int i = 0; i < r->loop_count; ++i) {
        const SStrokeLoop *l = &r->loops[i];
        n += fprintf(fp, "%d,%d,%d,%.12g,%d,%d,%d,%.12g,%.12g,%d,%d,\"",
                     l->index, l->loop_id, l->count, l->area, l->pair,
                     l->ea, l->eb, l->at.x, l->at.y,
                     l->hit_pair_count, l->hit_pair_total);
        for (int q = 0; q < l->hit_pair_count; ++q)
            n += fprintf(fp, "%s%d", q ? " " : "", l->hit_pairs[q]);
        n += fprintf(fp, "\"\n");
    }
    return n;
}

const char *stroke_strerror(int code) {
    switch (code) {
    case STROKE_OK:           return "ok";
    case STROKE_E_ARG:        return "invalid argument";
    case STROKE_E_ZERO_EDGE:  return "zero-length edge";
    case STROKE_E_ENDPOINT:   return "intersection at an endpoint";
    case STROKE_E_COLLINEAR:  return "collinear overlapping segments";
    case STROKE_E_CAP:        return "capacity exceeded";
    case STROKE_E_INTERNAL:   return "internal inconsistency";
    default:                  return "unknown";
    }
}
