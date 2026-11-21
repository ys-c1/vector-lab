#include "merge.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define EPS 1e-9

static char g_err[160];

static void set_err(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(g_err, sizeof(g_err), fmt, ap);
    va_end(ap);
}

const char *merge_last_error(void) { return g_err; }

static double cross(double ax, double ay, double bx, double by) {
    return ax * by - ay * bx;
}

static double pdist(const MPoint *p, const MPoint *q) {
    double dx = p->x - q->x, dy = p->y - q->y;
    return sqrt(dx * dx + dy * dy);
}

static int same_pt(const MPoint *p, const MPoint *q) {
    return pdist(p, q) <= EPS;
}

/* 有向面积：>0 逆时针(CCW)，<0 顺时针(CW) */
static double signed_area(const MPoint *p, int n) {
    double s = 0.0;
    for (int i = 0; i < n; ++i) {
        const MPoint *a = &p[i], *b = &p[(i + 1) % n];
        s += a->x * b->y - b->x * a->y;
    }
    return s * 0.5;
}

double merge_ring_area(const MRing *r) {
    if (!r || r->count < 3) return 0.0;
    return fabs(signed_area(r->pts, r->count));
}

double merge_union_area(const MMerge *m) {
    if (!m) return 0.0;
    double s = 0.0;
    for (int i = 0; i < m->n_rings; ++i) s += merge_ring_area(&m->rings[i]);
    return s;
}

/* 线段相交（含端点）。交点写入 hit；平行/共线不产生交点返回 0。 */
static int seg_hit(const MPoint *a0, const MPoint *a1,
                   const MPoint *b0, const MPoint *b1, MPoint *hit) {
    double rx = a1->x - a0->x, ry = a1->y - a0->y;
    double sx = b1->x - b0->x, sy = b1->y - b0->y;
    double den = cross(rx, ry, sx, sy);
    if (fabs(den) < EPS) return 0;
    double qx = b0->x - a0->x, qy = b0->y - a0->y;
    double t = cross(qx, qy, sx, sy) / den;
    double u = cross(qx, qy, rx, ry) / den;
    if (t < -EPS || t > 1.0 + EPS || u < -EPS || u > 1.0 + EPS) return 0;
    if (t < 0.0) t = 0.0;
    if (t > 1.0) t = 1.0;
    if (hit) {
        hit->x = a0->x + rx * t;
        hit->y = a0->y + ry * t;
    }
    return 1;
}

typedef struct { MPoint from, to; int seg; } Hedge;

/* 收集两段所有边的交点，去重后写入 hits，返回个数；-1 表示容量不足。 */
static int collect_hits(const MPoint *a, int na, const MPoint *b, int nb,
                        MPoint *hits, int cap) {
    int nh = 0;
    MPoint tmp[MERGE_RING_CAP];
    for (int i = 0; i < na; ++i) {
        const MPoint *a0 = &a[i], *a1 = &a[(i + 1) % na];
        for (int j = 0; j < nb; ++j) {
            const MPoint *b0 = &b[j], *b1 = &b[(j + 1) % nb];
            MPoint h;
            if (seg_hit(a0, a1, b0, b1, &h)) {
                int dup = 0;
                for (int k = 0; k < nh; ++k)
                    if (same_pt(&h, &tmp[k])) { dup = 1; break; }
                if (!dup) {
                    if (nh >= cap) return -1;
                    tmp[nh++] = h;
                }
            }
        }
    }
    for (int i = 0; i < nh; ++i) hits[i] = tmp[i];
    return nh;
}

/* 点是否落在线段 ab 上（含端点，EPS 容差） */
static int on_segment(const MPoint *p, const MPoint *a, const MPoint *b) {
    double cr = (b->x - a->x) * (p->y - a->y) - (b->y - a->y) * (p->x - a->x);
    if (fabs(cr) > EPS) return 0;
    double dot = (p->x - a->x) * (p->x - b->x) + (p->y - a->y) * (p->y - b->y);
    return dot <= EPS;
}

/* 严格点在多边形内：边界上返回 0（既不内也不外），内部 1，外部 -1 */
static int point_strict(const MPoint *p, const MPoint *poly, int n) {
    for (int i = 0; i < n; ++i)
        if (on_segment(p, &poly[i], &poly[(i + 1) % n])) return 0;
    int inside = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const MPoint *a = &poly[j], *b = &poly[i];
        if ((a->y > p->y) != (b->y > p->y)) {
            double xat = a->x + (p->y - a->y) * (b->x - a->x) / (b->y - a->y);
            if (xat > p->x) inside = !inside;
        }
    }
    return inside ? 1 : -1;
}

/*
 * 切点重合预检。对每个交点 h，检查两段在 h 处的局部穿越拓扑：
 * 正常穿越要求每段各有一条出边严格位于另一段边界的两侧；若 h 是切点
 * （两段在 h 处只触不穿，出边全在同一侧），或发现共线重叠边，即判退化。
 * 返回 1=发现切点/共线退化，0=干净穿越。
 */
static int find_tangency(const MPoint *a, int na, const MPoint *b, int nb,
                         const MPoint *hits, int nh, MPoint *where) {
    for (int k = 0; k < nh; ++k) {
        const MPoint *h = &hits[k];
        /* 段 a 在 h 处邻边远端相对 b 的严格侧别：1=内, 2=外；边界不计。
           h 为 a 的顶点 -> 两条邻边各取远端；h 在 a 某条边内部 -> 取该边两端。
           h 可能是插值交点（与顶点有 1e-15 级偏差），统一用 on_segment 识别。 */
        int a_side = 0;
        for (int i = 0; i < na; ++i) {
            const MPoint *v0 = &a[i], *v1 = &a[(i + 1) % na];
            if (!on_segment(h, v0, v1)) continue;
            if (on_segment(h, v0, v0)) {
                int s = point_strict(v1, b, nb);
                if (s > 0) a_side |= 1; else if (s < 0) a_side |= 2;
            } else if (on_segment(h, v1, v1)) {
                int s = point_strict(v0, b, nb);
                if (s > 0) a_side |= 1; else if (s < 0) a_side |= 2;
            } else {
                int s0 = point_strict(v0, b, nb), s1 = point_strict(v1, b, nb);
                if (s0 > 0) a_side |= 1; else if (s0 < 0) a_side |= 2;
                if (s1 > 0) a_side |= 1; else if (s1 < 0) a_side |= 2;
            }
        }
        int b_side = 0;
        for (int j = 0; j < nb; ++j) {
            const MPoint *v0 = &b[j], *v1 = &b[(j + 1) % nb];
            if (!on_segment(h, v0, v1)) continue;
            if (on_segment(h, v0, v0)) {
                int s = point_strict(v1, a, na);
                if (s > 0) b_side |= 1; else if (s < 0) b_side |= 2;
            } else if (on_segment(h, v1, v1)) {
                int s = point_strict(v0, a, na);
                if (s > 0) b_side |= 1; else if (s < 0) b_side |= 2;
            } else {
                int s0 = point_strict(v0, a, na), s1 = point_strict(v1, a, na);
                if (s0 > 0) b_side |= 1; else if (s0 < 0) b_side |= 2;
                if (s1 > 0) b_side |= 1; else if (s1 < 0) b_side |= 2;
            }
        }
        /* 任一段两侧不全（只在一侧或全在边界上）=> 切点而非穿越 */
        if (a_side != 3 || b_side != 3) {
            *where = *h;
            return 1;
        }
    }
    /* 顶点贴在对方边上但不属于任何穿越交点：切点/贴边（共边相切由此捕获） */
    for (int pass = 0; pass < 2; ++pass) {
        const MPoint *p = pass == 0 ? a : b;
        int np = pass == 0 ? na : nb;
        const MPoint *q = pass == 0 ? b : a;
        int nq = pass == 0 ? nb : na;
        for (int i = 0; i < np; ++i) {
            for (int j = 0; j < nq; ++j) {
                if (!on_segment(&p[i], &q[j], &q[(j + 1) % nq])) continue;
                int is_hit = 0;
                for (int k = 0; k < nh; ++k)
                    if (same_pt(&p[i], &hits[k])) { is_hit = 1; break; }
                if (!is_hit) {
                    *where = p[i];
                    return 1;
                }
            }
        }
    }
    /* 共线重叠边：一条边整体落在另一段的某条边上 */
    for (int i = 0; i < na; ++i) {
        const MPoint *a0 = &a[i], *a1 = &a[(i + 1) % na];
        for (int j = 0; j < nb; ++j) {
            const MPoint *b0 = &b[j], *b1 = &b[(j + 1) % nb];
            double cr = (a1->x - a0->x) * (b1->y - b0->y)
                      - (a1->y - a0->y) * (b1->x - b0->x);
            if (fabs(cr) > EPS) continue;
            if ((on_segment(a0, b0, b1) && on_segment(a1, b0, b1)) ||
                (on_segment(b0, a0, a1) && on_segment(b1, a0, a1))) {
                *where = *a0;
                return 1;
            }
        }
    }
    return 0;
}

/*
 * 用交点切分闭合环：对每条原边，收集其上的所有切分点，按参数 t 排序，
 * 依次输出子边。子边写入 edges（首尾点对），返回子边数；-1 表示容量不足。
 */
static int split_ring(const MPoint *poly, int n, const MPoint *hits, int nh,
                      int seg_id, Hedge *edges, int cap) {
    int ne = 0;
    for (int i = 0; i < n; ++i) {
        MPoint p0 = poly[i], p1 = poly[(i + 1) % n];
        double rx = p1.x - p0.x, ry = p1.y - p0.y;
        MPoint chain[MERGE_RING_CAP];
        double tv[MERGE_RING_CAP];
        int nc = 0;
        chain[nc] = p0; tv[nc] = 0.0; ++nc;
        for (int k = 0; k < nh; ++k) {
            double cr = cross(rx, ry, hits[k].x - p0.x, hits[k].y - p0.y);
            if (fabs(cr) > EPS) continue;
            double t;
            if (fabs(rx) >= fabs(ry)) t = (hits[k].x - p0.x) / rx;
            else t = (hits[k].y - p0.y) / ry;
            if (t < -EPS || t > 1.0 + EPS) continue;
            if (t < 0.0) t = 0.0;
            if (t > 1.0) t = 1.0;
            int dup = 0;
            for (int q = 0; q < nc; ++q) if (fabs(tv[q] - t) <= EPS) { dup = 1; break; }
            if (dup) continue;
            if (nc >= MERGE_RING_CAP - 1) return -1;
            chain[nc] = hits[k]; tv[nc] = t; ++nc;
            /* 简单插入排序（每边切分点很少） */
            for (int q = nc - 1; q > 0 && tv[q] < tv[q - 1]; --q) {
                double tt = tv[q]; tv[q] = tv[q - 1]; tv[q - 1] = tt;
                MPoint tp = chain[q]; chain[q] = chain[q - 1]; chain[q - 1] = tp;
            }
        }
        chain[nc] = p1; tv[nc] = 1.0; ++nc;
        for (int q = 0; q + 1 < nc; ++q) {
            if (same_pt(&chain[q], &chain[q + 1])) continue;
            if (ne >= cap) return -1;
            edges[ne].from = chain[q];
            edges[ne].to = chain[q + 1];
            edges[ne].seg = seg_id;
            ++ne;
        }
    }
    return ne;
}

/* 在 edges 中找以 head 为起点的幸存边索引；返回 -1=无，-2=多于一条（切点重合） */
static int next_edge(const Hedge *edges, int ne, const MPoint *head, int *candidates) {
    int idx = -1, cnt = 0;
    for (int i = 0; i < ne; ++i) {
        if (same_pt(&edges[i].from, head)) {
            ++cnt;
            idx = i;
        }
    }
    if (candidates) *candidates = cnt;
    if (cnt == 0) return -1;
    if (cnt > 1) return -2;
    return idx;
}

int merge_polygons(const MPoint *a, int na, int id_a,
                   const MPoint *b, int nb, int id_b,
                   int union_on, MMerge *out) {
    g_err[0] = '\0';
    if (!a || !b || !out || na < 3 || nb < 3) {
        set_err("bad argument: need >=3 points each (seg ids %d/%d)", id_a, id_b);
        return MERGE_E_ARG;
    }
    memset(out, 0, sizeof(*out));
    out->seg_a = id_a;
    out->seg_b = id_b;
    out->union_on = union_on ? 1 : 0;

    /* 开关关闭：直接回原两段，不做任何几何处理 */
    if (!union_on) {
        if (na > MERGE_RING_CAP || nb > MERGE_RING_CAP)
            return MERGE_E_CAP;
        out->n_rings = 2;
        out->rings[0].count = na;
        for (int i = 0; i < na; ++i) out->rings[0].pts[i] = a[i];
        out->rings[1].count = nb;
        for (int i = 0; i < nb; ++i) out->rings[1].pts[i] = b[i];
        return MERGE_OK;
    }

    /* 绕序检查：两段必须同为逆时针（有向面积同号且为正） */
    double sa = signed_area(a, na), sb = signed_area(b, nb);
    if (fabs(sa) < EPS || fabs(sb) < EPS) {
        set_err("seg %d/%d winding conflict: degenerate ring (area ~ 0)", id_a, id_b);
        return MERGE_E_WIND;
    }
    if ((sa > 0.0) != (sb > 0.0)) {
        set_err("seg %d/%d winding conflict: seg %d is %s, seg %d is %s",
                id_a, id_b,
                id_a, sa > 0 ? "CCW" : "CW",
                id_b, sb > 0 ? "CCW" : "CW");
        return MERGE_E_WIND;
    }

    int nh = collect_hits(a, na, b, nb, out->hits, MERGE_RING_CAP);
    if (nh < 0) {
        set_err("seg %d/%d too many intersection points", id_a, id_b);
        return MERGE_E_CAP;
    }
    out->n_hits = nh;

    /* 切点重合 / 共线退化预检：命中即报错并附两段编号与位置 */
    MPoint twhere;
    if (find_tangency(a, na, b, nb, out->hits, nh, &twhere)) {
        set_err("seg %d/%d tangent/coincident point at (%.4f, %.4f): "
                "boundaries touch without a clean crossing",
                id_a, id_b, twhere.x, twhere.y);
        return MERGE_E_TANGENT;
    }

    /* 无交点：相离或包含，两段各自成环 */
    if (nh == 0) {
        if (na > MERGE_RING_CAP || nb > MERGE_RING_CAP) return MERGE_E_CAP;
        out->n_rings = 2;
        out->rings[0].count = na;
        for (int i = 0; i < na; ++i) out->rings[0].pts[i] = a[i];
        out->rings[1].count = nb;
        for (int i = 0; i < nb; ++i) out->rings[1].pts[i] = b[i];
        return MERGE_OK;
    }

    /* 1) 在交点处切分两段 */
    Hedge edges[2 * MERGE_RING_CAP];
    int nea = split_ring(a, na, out->hits, nh, id_a, edges,
                         (int)(sizeof(edges) / sizeof(edges[0])));
    if (nea < 0) return MERGE_E_CAP;
    int neb = split_ring(b, nb, out->hits, nh, id_b, edges + nea,
                         (int)(sizeof(edges) / sizeof(edges[0])) - nea);
    if (neb < 0) return MERGE_E_CAP;
    int ne = nea + neb;

    /* 2) 保留并集边界：子边中点严格不在另一段内部才保留。
       边界上的中点（共边）不算在内 -> 共边丢弃，避免交叠区被两段各算一次。
       必须用严格判定：凹多边形的共边中点可能落在对方凹口内部，把边界点
       误判为“内”会保留本应删除的边，使交叠区被两段各算一次而面积虚高。 */
    Hedge keep[2 * MERGE_RING_CAP];
    int nk = 0;
    for (int i = 0; i < ne; ++i) {
        MPoint mid;
        mid.x = (edges[i].from.x + edges[i].to.x) * 0.5;
        mid.y = (edges[i].from.y + edges[i].to.y) * 0.5;
        const MPoint *other = edges[i].seg == id_a ? b : a;
        int on = edges[i].seg == id_a ? nb : na;
        if (point_strict(&mid, other, on) < 0) {
            if (nk >= (int)(sizeof(keep) / sizeof(keep[0]))) return MERGE_E_CAP;
            keep[nk++] = edges[i];
        }
    }

    /* 3) 按绕序串外轮廓：每个切点处幸存边必须唯一续接，否则即切点重合/退化 */
    char used[2 * MERGE_RING_CAP];
    memset(used, 0, (size_t)nk);
    int used_cnt = 0, steps = 0;
    while (used_cnt < nk) {
        if (out->n_rings >= MERGE_MAX_RINGS) return MERGE_E_CAP;
        int s = 0;
        while (s < nk && used[s]) ++s;
        MRing *ring = &out->rings[out->n_rings];
        ring->count = 0;
        MPoint head = keep[s].from;
        int cur = s;
        for (;;) {
            if (ring->count >= MERGE_RING_CAP) return MERGE_E_CAP;
            ring->pts[ring->count++] = keep[cur].from;
            used[cur] = 1;
            ++used_cnt;
            MPoint nxt = keep[cur].to;
            ++steps;
            if (same_pt(&nxt, &head)) break;
            if (steps > nk + 1) {
                set_err("seg %d/%d tangent/coincident point: ring walk did not close",
                        id_a, id_b);
                return MERGE_E_TANGENT;
            }
            int cand = 0;
            int ni = next_edge(keep, nk, &nxt, &cand);
            if (ni == -2) {
                set_err("seg %d/%d tangent/coincident point at (%.4f, %.4f): "
                        "%d surviving edges, continuation not unique",
                        id_a, id_b, nxt.x, nxt.y, cand);
                return MERGE_E_TANGENT;
            }
            if (ni == -1) {
                set_err("seg %d/%d tangent/coincident point at (%.4f, %.4f): "
                        "dangling boundary edge",
                        id_a, id_b, nxt.x, nxt.y);
                return MERGE_E_TANGENT;
            }
            cur = ni;
        }
        if (ring->count >= 3) ++out->n_rings;
    }
    if (out->n_rings == 0) {
        set_err("seg %d/%d produced no outer ring", id_a, id_b);
        return MERGE_E_TANGENT;
    }
    return MERGE_OK;
}
