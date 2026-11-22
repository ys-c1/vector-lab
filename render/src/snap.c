#include "snap.h"
#include <math.h>
#include <string.h>

#define SNAP_AREA_REF 1e-12

static double shoelace(const RPoly *p) {
    double s = 0.0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        s += a.x * b.y - b.x * a.y;
    }
    return s * 0.5;
}

static double shoelace_mag(const RPoly *p) {
    double s = 0.0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        s += fabs(a.x * b.y - b.x * a.y);
    }
    return s * 0.5;
}

const char *snap_status_name(int status) {
    switch (status) {
    case SNAP_OK: return "ok";
    case SNAP_INPUT_DEGEN: return "input-degenerate";
    case SNAP_DEG_POINT: return "degenerate-point";
    case SNAP_DEG_SEGMENT: return "degenerate-segment";
    case SNAP_DEG_ZERO: return "degenerate-zero-area";
    case SNAP_DEG_FLIP: return "degenerate-orientation-flip";
    case SNAP_DEG_GROW: return "degenerate-grown";
    default: return "?";
    }
}

/* 输入多边形本身的退化分类：全顶点重合=1，共线=2，有顶点但面积抵消=3。 */
int snap_input_kind(const RPoly *p) {
    if (!p || p->n < 3) return 1;
    int same = 1;
    for (int i = 1; i < p->n && same; ++i)
        if (p->pts[i].x != p->pts[0].x || p->pts[i].y != p->pts[0].y) same = 0;
    if (same) return 1;
    double sx = 0.0, sy = 0.0, m = 0.0;
    for (int i = 0; i < p->n; ++i) {
        sx += p->pts[i].x; sy += p->pts[i].y;
        m += fabs(p->pts[i].x) + fabs(p->pts[i].y);
    }
    sx /= p->n; sy /= p->n;
    double scale = m / p->n + 1.0;
    double worst = 0.0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        double ax = a.x - sx, ay = a.y - sy, bx = b.x - sx, by = b.y - sy;
        double cr = fabs(ax * by - ay * bx);
        if (cr > worst) worst = cr;
    }
    if (worst <= 1e-9 * scale * scale) return 2;
    return 3;
}

static int degenerate_status(int status) {
    return status == SNAP_DEG_POINT || status == SNAP_DEG_SEGMENT
        || status == SNAP_DEG_ZERO || status == SNAP_DEG_FLIP
        || status == SNAP_DEG_GROW;
}

int snap_eval(const RScene *s, double px, double py, double ox, double oy,
              int coarse, int m, SnapEval *ev, RScene *out) {
    if (!s || !ev || !out || !(px > 0.0) || !(py > 0.0) || m < 1) return -1;
    if (s->npolys > SNAP_MAX_POLY) return -2;
    double hx = coarse ? px * (double)m : px / (double)m;
    double hy = coarse ? py * (double)m : py / (double)m;
    if (!isfinite(hx) || hx <= 0.0 || !isfinite(hy) || hy <= 0.0) return -3;

    memset(ev, 0, sizeof(*ev));
    ev->hx = hx;
    ev->hy = hy;
    ev->px = px;
    ev->py = py;
    ev->coarse = coarse;
    ev->m = m;
    ev->npolys = s->npolys;
    ev->worst_poly = -1;
    ev->first_bad = -1;

    scene_init(out);
    for (int k = 0; k < s->npolys; ++k) {
        const RPoly *src = &s->polys[k];
        RPoly dst = *src;
        long long gx[64], gy[64];
        int distinct = 0;
        for (int i = 0; i < src->n; ++i) {
            gx[i] = (long long)llround((src->pts[i].x - ox) / hx);
            gy[i] = (long long)llround((src->pts[i].y - oy) / hy);
            dst.pts[i].x = ox + (double)gx[i] * hx;
            dst.pts[i].y = oy + (double)gy[i] * hy;
            int seen = 0;
            for (int j = 0; j < i && !seen; ++j)
                if (gx[j] == gx[i] && gy[j] == gy[i]) seen = 1;
            if (!seen) ++distinct;
        }
        ev->input_kind[k] = (unsigned char)snap_input_kind(src);

        double a0 = shoelace(src);
        double a1 = shoelace(&dst);
        double ref = shoelace_mag(src);
        double tolA = SNAP_AREA_REF * (ref + 1.0);
        int degenerate_in = fabs(a0) <= tolA;

        unsigned char st;
        if (degenerate_in) {
            st = fabs(a1) > tolA ? SNAP_DEG_GROW : SNAP_INPUT_DEGEN;
        } else if (distinct <= 1) {
            st = SNAP_DEG_POINT;
        } else if (distinct == 2) {
            st = SNAP_DEG_SEGMENT;
        } else if (fabs(a1) <= tolA) {
            st = SNAP_DEG_ZERO;
        } else if ((a0 > 0.0) != (a1 > 0.0)) {
            st = SNAP_DEG_FLIP;
        } else {
            st = SNAP_OK;
        }

        ev->status[k] = st;
        if (degenerate_status(st) && ev->first_bad < 0) ev->first_bad = k;
        ev->area0 += fabs(a0);
        ev->area1 += fabs(a1);
        if (st == SNAP_OK) {
            double e = fabs(fabs(a1) - fabs(a0)) / fabs(a0);
            if (e > ev->worst_err) { ev->worst_err = e; ev->worst_poly = k; }
        }
        if (scene_add_poly(out, dst.pts, dst.n) != 0) return -4;
    }
    if (!(ev->area0 > 0.0)) {
        ev->err = 0.0;
        return 1; /* 全部输入退化，无面积误差可言 */
    }
    ev->err = fabs(ev->area1 - ev->area0) / ev->area0;
    return 0;
}

int snap_ladder(const RScene *s, double px, double py, double ox, double oy,
                int max_m, SnapLadder *lad) {
    if (!s || !lad || !(px > 0.0) || !(py > 0.0) || max_m < 1) return -1;
    lad->n = 0;
    RScene tmp;
    for (int pass = 0; pass < 2; ++pass) {
        int from = (pass == 0) ? max_m : 1;
        int to = (pass == 0) ? 1 : max_m;
        int step = (pass == 0) ? -1 : 1;
        for (int m = from; step < 0 ? m >= to : m <= to; m += step) {
            if (pass == 1 && m == 1) continue;
            if (lad->n >= SNAP_MAX_ROWS) return 0;
            int rc = snap_eval(s, px, py, ox, oy, pass == 0, m, &lad->rows[lad->n], &tmp);
            if (rc < 0) return rc;
            ++lad->n;
        }
    }
    return 0;
}

int snap_solve(const RScene *s, double px, double py, double ox, double oy,
               double tol, int max_m, SnapSolve *sol, RScene *out) {
    if (!s || !sol || !out || !(px > 0.0) || !(py > 0.0) || !(tol > 0.0) || max_m < 1)
        return -1;
    memset(sol, 0, sizeof(*sol));
    sol->rejected_poly = -1;
    for (int k = 0; k < SNAP_MAX_POLY; ++k) sol->collapse[k].finest_status = SNAP_OK;

    SnapEval ev;
    RScene tmp;
    SnapEval best;
    RScene best_scene;
    int have_best = 0;

    /* 全部输入本身无面积：没有可对的误差，固定按一个像素吸附。 */
    int any_area = 0;
    for (int k = 0; k < s->npolys; ++k)
        if (fabs(shoelace(&s->polys[k])) > SNAP_AREA_REF) any_area = 1;
    if (!any_area) {
        int rc0 = snap_eval(s, px, py, ox, oy, 1, 1, &ev, &tmp);
        if (rc0 < 0) return rc0;
        sol->chosen = ev;
        sol->rc = 0;
        sol->evaluated = 1;
        sol->all_input_degenerate = 1;
        *out = tmp;
        return 0;
    }

    /* 从粗到细扫：第一个“不塌陷且误差在预算内”的即解——它是最粗的可行步长
     * （吸附强度最大）。粗侧 m=max_m→1，接着细侧 m=2→max_m。 */
    for (int pass = 0; pass < 2; ++pass) {
        int from = (pass == 0) ? max_m : 1;
        int to = (pass == 0) ? 1 : max_m;
        int step = (pass == 0) ? -1 : 1;
        for (int m = from; step < 0 ? m >= to : m <= to; m += step) {
            if (pass == 1 && m == 1) continue; /* 粗侧已试过 h=p */
            int rc = snap_eval(s, px, py, ox, oy, pass == 0, m, &ev, &tmp);
            if (rc < 0) return rc;
            ++sol->evaluated;
            /* 塌陷随步长非单调，逐档累积实测区间。 */
            for (int k = 0; k < s->npolys; ++k) {
                int st = ev.status[k];
                if (st <= SNAP_INPUT_DEGEN) continue;
                SnapCollapse *c = &sol->collapse[k];
                if (c->count == 0) {
                    c->min_hx = c->max_hx = ev.hx;
                    c->min_hy = c->max_hy = ev.hy;
                }
                if (ev.hx < c->min_hx) { c->min_hx = ev.hx; c->min_hy = ev.hy; }
                if (ev.hx > c->max_hx) { c->max_hx = ev.hx; c->max_hy = ev.hy; }
                if (st == SNAP_DEG_POINT) {
                    if (c->point_count == 0) {
                        c->point_min_hx = c->point_max_hx = ev.hx;
                        c->point_min_hy = c->point_max_hy = ev.hy;
                    }
                    if (ev.hx < c->point_min_hx) { c->point_min_hx = ev.hx; c->point_min_hy = ev.hy; }
                    if (ev.hx > c->point_max_hx) { c->point_max_hx = ev.hx; c->point_max_hy = ev.hy; }
                    ++c->point_count;
                }
                c->finest_status = st;
                ++c->count;
            }
            if (ev.first_bad >= 0) {
                sol->have_rejected = 1;
                sol->rejected_hx = ev.hx;
                sol->rejected_hy = ev.hy;
                sol->rejected_poly = ev.first_bad;
                sol->rejected_status = ev.status[ev.first_bad];
            }
            if (ev.first_bad >= 0 || rc != 0) continue;
            /* 总量与单图形两个口径都要在预算内：不让任何单个图形漂过 tol。
             * 命中即返回——这是阶梯上最粗的可行步长。 */
            if (ev.err <= tol && ev.worst_err <= tol) {
                sol->chosen = ev;
                sol->rc = 0;
                *out = tmp;
                return 0;
            }
            if (!have_best || ev.err + ev.worst_err < best.err + best.worst_err) {
                best = ev;
                best_scene = tmp;
                have_best = 1;
            }
        }
    }

    /* 无预算内解：退回非塌陷候选里误差最小的一个（rc=1 显式标出超预算）。 */
    if (have_best) {
        sol->chosen = best;
        sol->rc = 1;
        *out = best_scene;
        return 1;
    }
    sol->rc = 2;
    return 2;
}
