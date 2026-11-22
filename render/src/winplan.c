#define _POSIX_C_SOURCE 199309L
#include "winplan.h"
#include "raster.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
}

int winplan_default_opt(RWinPlanOpt *o) {
    if (!o) return WINPLAN_ERR_ARG;
    o->win_w = 40;
    o->win_h = 40;
    o->stride = 8;
    o->target = 0.98;
    o->overlap_cap = 0.60;
    o->max_wins = WINPLAN_MAX_WINS;
    o->fixed_count = 0;
    return WINPLAN_OK;
}

static int opt_valid(const RWinPlanOpt *o) {
    if (!o) return 0;
    if (o->win_w <= 0 || o->win_h <= 0) return 0;
    if (o->stride <= 0) return 0;
    if (!(o->target > 0.0 && o->target <= 1.0)) return 0;
    if (!(o->overlap_cap >= 0.0 && o->overlap_cap < 1.0)) return 0;
    if (o->max_wins <= 0 || o->max_wins > WINPLAN_MAX_WINS) return 0;
    if (o->fixed_count < 0 || o->fixed_count > WINPLAN_MAX_WINS) return 0;
    return 1;
}

/* 一条轴上的候选点数：0, stride, ... 再补贴边点（若没落在格点上）。 */
static int npos_axis(int dim, int win, int stride) {
    if (win > dim) return 0;
    int n = (dim - win) / stride + 1;
    if ((dim - win) % stride != 0) ++n;
    return n;
}

static int cand_pos(int dim, int win, int stride, int k) {
    if (win >= dim) return 0;
    int nreg = (dim - win) / stride;
    if (k <= nreg) return k * stride;
    return dim - win;
}

int winplan_candidate_count(int w, int h, const RWinPlanOpt *o) {
    if (w <= 0 || h <= 0 || !opt_valid(o)) return -1;
    int nx = npos_axis(w, o->win_w, o->stride);
    int ny = npos_axis(h, o->win_h, o->stride);
    if (nx == 0 || ny == 0) return -1;
    if ((long long)nx * ny > 1 << 20) return -1;
    return nx * ny;
}

typedef struct { long long lo, hi; } Span;

static int cmp_span(const void *a, const void *b) {
    const Span *x = (const Span *)a, *y = (const Span *)b;
    if (x->lo != y->lo) return (x->lo > y->lo) - (x->lo < y->lo);
    return (x->hi > y->hi) - (x->hi < y->hi);
}
static int cmp_ll(const void *a, const void *b) {
    long long x = *(const long long *)a, y = *(const long long *)b;
    return (x > y) - (x < y);
}

/* 像素矩形并集面积：x 扫描线切条，条内合并 y 区间；全整数，结果精确。 */
static long long rect_union_area(const RWin *wins, int n) {
    if (n <= 0) return 0;
    long long xs[2 * WINPLAN_MAX_WINS];
    Span spans[WINPLAN_MAX_WINS];
    int nxs = 0;
    for (int i = 0; i < n; ++i) {
        xs[nxs++] = wins[i].x;
        xs[nxs++] = (long long)wins[i].x + wins[i].w;
    }
    qsort(xs, (size_t)nxs, sizeof(long long), cmp_ll);
    long long area = 0;
    for (int i = 0; i + 1 < nxs; ++i) {
        long long a = xs[i], b = xs[i + 1];
        if (b <= a) continue;
        int m = 0;
        for (int k = 0; k < n; ++k)
            if (wins[k].x <= a && (long long)wins[k].x + wins[k].w >= b) {
                spans[m].lo = wins[k].y;
                spans[m].hi = (long long)wins[k].y + wins[k].h;
                ++m;
            }
        if (m == 0) continue;
        qsort(spans, (size_t)m, sizeof(Span), cmp_span);
        long long tot = 0, cs = spans[0].lo, ce = spans[0].hi;
        for (int p = 1; p < m; ++p) {
            if (spans[p].lo > ce) {
                tot += ce - cs;
                cs = spans[p].lo;
                ce = spans[p].hi;
            } else if (spans[p].hi > ce) {
                ce = spans[p].hi;
            }
        }
        tot += ce - cs;
        area += tot * (b - a);
    }
    return area;
}

const char *winplan_stop_str(int stop) {
    switch (stop) {
    case WINPLAN_STOP_FULL: return "FULL";
    case WINPLAN_STOP_TARGET: return "TARGET";
    case WINPLAN_STOP_OVERLAP: return "OVERLAP";
    case WINPLAN_STOP_NOGAIN: return "NOGAIN";
    case WINPLAN_STOP_MAXWIN: return "MAXWIN";
    case WINPLAN_STOP_FIXED: return "FIXED";
    case WINPLAN_STOP_EMPTY: return "EMPTY";
    default: return "?";
    }
}

/* 固定窗口数对照：按候选扫描序（先小 y 后小 x）取前 N 个，不做贪心、
 * 不查重叠上限。用来复现“窗口数量定死、有些区域根本没被覆盖”的旧做法，
 * 与贪心结果并列比较。 */
static int plan_fixed(const unsigned char *on, int w, int h,
                      const RWinPlanOpt *o, RWinPlan *out, int nx) {
    unsigned char *cov = (unsigned char *)calloc((size_t)w * h, 1);
    if (!cov) return WINPLAN_ERR_NOMEM;
    int total = out->on_pixels;
    int ny = npos_axis(h, o->win_h, o->stride);
    int nwins = o->fixed_count;
    if (nwins > nx * ny) nwins = nx * ny;   /* 候选不够就只取现有的，如实报告 */
    int covered = 0;
    for (int k = 0; k < nwins; ++k) {
        int x0 = cand_pos(w, o->win_w, o->stride, k % nx);
        int y0 = cand_pos(h, o->win_h, o->stride, k / nx);
        out->wins[k] = (RWin){ x0, y0, o->win_w, o->win_h };
        int g = 0;
        for (int y = y0; y < y0 + o->win_h; ++y) {
            unsigned char *row = cov + (size_t)y * w;
            const unsigned char *orow = on + (size_t)y * w;
            for (int x = x0; x < x0 + o->win_w; ++x)
                if (orow[x] && !row[x]) { row[x] = 1; ++g; }
        }
        out->gain[k] = g;
        covered += g;
    }
    out->nwins = nwins;
    out->covered = covered;
    out->coverage = total ? (double)covered / (double)total : 1.0;
    out->sum_area = (long long)nwins * o->win_w * o->win_h;
    out->union_area = rect_union_area(out->wins, nwins);
    out->overlap = out->sum_area > 0
        ? (double)(out->sum_area - out->union_area) / (double)out->sum_area : 0.0;
    out->stop = WINPLAN_STOP_FIXED;
    free(cov);
    return WINPLAN_OK;
}

int winplan_plan(const unsigned char *on, int w, int h,
                 const RWinPlanOpt *o, RWinPlan *out) {
    if (!on || !out || w <= 0 || h <= 0 || !opt_valid(o)) return WINPLAN_ERR_ARG;
    if (o->win_w > w || o->win_h > h) return WINPLAN_ERR_ARG;
    double t0 = now_ms();
    memset(out, 0, sizeof(*out));

    int nx = npos_axis(w, o->win_w, o->stride);
    int ny = npos_axis(h, o->win_h, o->stride);
    if (nx == 0 || ny == 0) return WINPLAN_ERR_ARG;
    long long ncand = (long long)nx * ny;
    if (ncand > (1 << 20)) return WINPLAN_ERR_ARG;   /* 防御：候选过多 */

    size_t npix = (size_t)w * (size_t)h;
    unsigned char *cov = (unsigned char *)calloc(npix, 1);
    long long *ps = (long long *)calloc((size_t)(h + 1) * (size_t)(w + 1),
                                        sizeof(long long));
    if (!cov || !ps) { free(cov); free(ps); return WINPLAN_ERR_NOMEM; }

    int total = 0;
    for (size_t i = 0; i < npix; ++i) if (on[i]) ++total;
    out->on_pixels = total;
    if (total == 0) {
        out->coverage = 1.0;
        out->overlap = 0.0;
        out->stop = WINPLAN_STOP_EMPTY;
        out->plan_ms = now_ms() - t0;
        free(cov); free(ps);
        return WINPLAN_OK;
    }

    int plan_cap = o->max_wins;
    if (o->fixed_count > 0) plan_cap = o->fixed_count;

    if (o->fixed_count > 0) {
        int prc = plan_fixed(on, w, h, o, out, nx);
        out->plan_ms = now_ms() - t0;
        free(cov); free(ps);
        return prc;
    }

    int covered = 0, nwins = 0, stop = WINPLAN_STOP_MAXWIN;
    long long sum_area = 0, union_area = 0;
    double overlap = 0.0;
    /* 被拒候选：固定候选全集很小，用位图记住，避免反复评估同一个。 */
    unsigned char *dead = (unsigned char *)calloc((size_t)ncand, 1);
    if (!dead) { free(cov); free(ps); return WINPLAN_ERR_NOMEM; }

    for (;;) {
        /* ps[j][i] = 行 <j、列 <i 的未覆盖图形像素数。每轮重建，O(w*h)。 */
        for (int x = 0; x <= w; ++x) ps[x] = 0;
        for (int y = 0; y < h; ++y) {
            long long row = 0;
            long long *prev = ps + (size_t)y * (w + 1);
            long long *cur = prev + (w + 1);
            cur[0] = 0;
            for (int x = 0; x < w; ++x) {
                if (on[y * w + x] && !cov[y * w + x]) ++row;
                cur[x + 1] = prev[x + 1] + row;
            }
        }
        /* 从增益最大的候选开始试；重叠超限的候选作废后继续找下一个。 */
        int pick = -1, bgain = 0;
        double new_ov = 0.0;
        long long new_union = 0, new_sum = 0;
        for (;;) {
            int bx = -1, by = -1, bk = -1, bg = 0;
            for (int k = 0; k < (int)ncand; ++k) {
                if (dead[k]) continue;
                int x0 = cand_pos(w, o->win_w, o->stride, k % nx);
                int y0 = cand_pos(h, o->win_h, o->stride, k / nx);
                int x1 = x0 + o->win_w, y1 = y0 + o->win_h;
                long long g = ps[(size_t)y1 * (w + 1) + x1]
                            - ps[(size_t)y0 * (w + 1) + x1]
                            - ps[(size_t)y1 * (w + 1) + x0]
                            + ps[(size_t)y0 * (w + 1) + x0];
                if (g > bg) { bg = (int)g; bx = x0; by = y0; bk = k; }
            }
            if (bk < 0 || bg == 0) break;
            out->wins[nwins] = (RWin){ bx, by, o->win_w, o->win_h };
            long long u = rect_union_area(out->wins, nwins + 1);
            long long s = sum_area + (long long)o->win_w * o->win_h;
            double ov = (double)(s - u) / (double)s;
            /* 固定窗口数对照模式不做重叠上限判断：它就是要展示“定死 N 个”的后果。 */
            if (o->fixed_count == 0 && ov > o->overlap_cap + 1e-12) {
                if (out->nrejects < WINPLAN_MAX_REJECTS) {
                    out->rejects[out->nrejects].x = bx;
                    out->rejects[out->nrejects].y = by;
                    out->rejects[out->nrejects].gain = bg;
                    out->rejects[out->nrejects].after_wins = nwins;
                    out->rejects[out->nrejects].overlap = ov;
                }
                ++out->nrejects;
                dead[bk] = 1;
                continue;
            }
            pick = bk; bgain = bg; new_ov = ov; new_union = u; new_sum = s;
            break;
        }
        if (pick < 0) {
            if (out->nrejects > 0) stop = WINPLAN_STOP_OVERLAP;
            else stop = o->fixed_count > 0 ? WINPLAN_STOP_FIXED : WINPLAN_STOP_NOGAIN;
            break;
        }
        if (nwins >= plan_cap) {
            stop = o->fixed_count > 0 ? WINPLAN_STOP_FIXED : WINPLAN_STOP_MAXWIN;
            break;
        }
        RWin r = out->wins[nwins];
        out->gain[nwins] = bgain;
        sum_area = new_sum;
        union_area = new_union;
        overlap = new_ov;
        ++nwins;
        for (int y = r.y; y < r.y + r.h; ++y) {
            unsigned char *row = cov + (size_t)y * w;
            const unsigned char *orow = on + (size_t)y * w;
            for (int x = r.x; x < r.x + r.w; ++x)
                if (orow[x] && !row[x]) { row[x] = 1; ++covered; }
        }
        if (covered == total) { stop = WINPLAN_STOP_FULL; break; }
        if ((double)covered / (double)total >= o->target - 1e-12) {
            stop = WINPLAN_STOP_TARGET; break;
        }
        if (nwins >= plan_cap) {
            stop = o->fixed_count > 0 ? WINPLAN_STOP_FIXED : WINPLAN_STOP_MAXWIN;
            break;
        }
    }
    free(dead);

    out->nwins = nwins;
    out->covered = covered;
    out->coverage = (double)covered / (double)total;
    out->sum_area = sum_area;
    out->union_area = union_area;
    out->overlap = overlap;
    out->stop = stop;
    out->plan_ms = now_ms() - t0;

    int rc = WINPLAN_OK;
    if (stop == WINPLAN_STOP_OVERLAP) rc = WINPLAN_ERR_OVERLAP;
    else if (stop == WINPLAN_STOP_NOGAIN || stop == WINPLAN_STOP_MAXWIN)
        rc = WINPLAN_ERR_TARGET;
    free(cov); free(ps);
    return rc;
}

int winplan_render(const RScene *s, RPlace place, RBox win,
                   const RWinPlan *p, int w, int h, unsigned char *buf) {
    if (!s || !p || !buf || w <= 0 || h <= 0) return WINPLAN_ERR_ARG;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -3;
    RScene moved;
    int rc = pipeline_transform(s, place, &moved);
    if (rc != 0) return rc;
    memset(buf, 0, (size_t)w * (size_t)h);
    for (int k = 0; k < p->nwins; ++k) {
        const RWin *r = &p->wins[k];
        rc = raster_fill_region(&moved, &win, w, h, 4, r->x, r->y, r->w, r->h, buf);
        if (rc != 0) return rc;
    }
    return 0;
}
