#include "fit.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---------------- 基础图元 ---------------- */

static double fcross(FPoint o, FPoint a, FPoint b) {
    return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
}

double fit_area(const FPoly *p) {
    if (!p || p->n < 3) return 0.0;
    double s = 0.0;
    for (int i = 0; i < p->n; ++i) {
        FPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        s += a.x * b.y - b.x * a.y;
    }
    return fabs(0.5 * s);
}

int fit_bbox(const FPoly *const *polys, int np, double box[4]) {
    if (!polys || !box || np < 1) return -1;
    int got = 0;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (int k = 0; k < np; ++k) {
        const FPoly *p = polys[k];
        if (!p || p->n < 1) return -1;
        for (int i = 0; i < p->n; ++i) {
            double x = p->pts[i].x, y = p->pts[i].y;
            if (!got) { minx = maxx = x; miny = maxy = y; got = 1; }
            if (x < minx) minx = x;
            if (x > maxx) maxx = x;
            if (y < miny) miny = y;
            if (y > maxy) maxy = y;
        }
    }
    box[0] = minx; box[1] = miny; box[2] = maxx; box[3] = maxy;
    return 0;
}

/* 窗口边 a->b，内部在左侧（叉积 >= 0），与 clip 模块同算法但容量更大 */
static int edge_inside(FPoint a, FPoint b, FPoint p) {
    return fcross(a, b, p) >= 0.0;
}

static FPoint edge_meet(FPoint a, FPoint b, FPoint c, FPoint d) {
    double a1 = b.y - a.y, b1 = a.x - b.x, c1 = a1 * a.x + b1 * a.y;
    double a2 = d.y - c.y, b2 = c.x - d.x, c2 = a2 * c.x + b2 * c.y;
    double det = a1 * b2 - a2 * b1;
    FPoint r = {0.0, 0.0};
    if (det != 0.0) {
        r.x = (b2 * c1 - b1 * c2) / det;
        r.y = (a1 * c2 - a2 * c1) / det;
    }
    return r;
}

int fit_clip(const FPoly *subj, const FPoly *win, FPoly *out) {
    if (!subj || !win || !out) return -1;
    if (subj->n < 3) return -2;
    if (win->n < 3) return -3;
    FPoly cur, nxt;
    cur.n = subj->n;
    if (cur.n > FIT_MAX) return -4;
    for (int i = 0; i < cur.n; ++i) cur.pts[i] = subj->pts[i];
    for (int e = 0; e < win->n && cur.n > 0; ++e) {
        FPoint a = win->pts[e], b = win->pts[(e + 1) % win->n];
        nxt.n = 0;
        for (int i = 0; i < cur.n; ++i) {
            FPoint p = cur.pts[i], q = cur.pts[(i + 1) % cur.n];
            int ip = edge_inside(a, b, p), iq = edge_inside(a, b, q);
            if (ip) {
                if (nxt.n >= FIT_MAX) return -4;
                nxt.pts[nxt.n++] = p;
            }
            if (ip != iq) {
                if (nxt.n >= FIT_MAX) return -4;
                nxt.pts[nxt.n++] = edge_meet(p, q, a, b);
            }
        }
        cur = nxt;
    }
    *out = cur;
    return 0;
}

int fit_regular(int n, double cx, double cy, double r, double theta, FPoly *out) {
    if (!out) return -1;
    if (n < 3) return -2;
    if (n > FIT_MAX) return -4;
    if (!(r > 0.0)) return -3;
    out->n = n;
    for (int i = 0; i < n; ++i) {
        double a = theta + 2.0 * M_PI * i / n;
        out->pts[i].x = cx + r * cos(a);
        out->pts[i].y = cy + r * sin(a);
    }
    return 0;
}

int fit_collinear(const FPoly *p, int *idx, int cap) {
    if (!p) return -1;
    if (p->n < 3) return -2;
    int cnt = 0;
    for (int i = 0; i < p->n; ++i) {
        FPoint a = p->pts[(i - 1 + p->n) % p->n];
        FPoint b = p->pts[i];
        FPoint c = p->pts[(i + 1) % p->n];
        double bx = b.x - a.x, by = b.y - a.y;
        double cx2 = c.x - a.x, cy2 = c.y - a.y;
        double cr = bx * cy2 - by * cx2;
        double scale = (hypot(bx, by) * hypot(cx2, cy2));
        if (scale > 0.0 && fabs(cr) <= 1e-9 * scale) {
            if (idx && cnt < cap) idx[cnt] = i;
            ++cnt;
        }
    }
    return cnt;
}

/* ---------------- 渲染（与 render 模块同公式，供逐字节比对） ---------------- */

static int point_in_poly(const FPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        FPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

int fit_write_pgm(const char *path, const FPoly *const *polys, int np,
                  const double box[4], int w, int h, int ss) {
    if (!path || !polys || !box || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (!(box[2] > box[0]) || !(box[3] > box[1])) return -3;
    unsigned char *buf = (unsigned char *)malloc((size_t)w * (size_t)h);
    if (!buf) return -7;
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box[2] - box[0]) / w, dy = (box[3] - box[1]) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss && !hit; ++j)
                for (int i = 0; i < ss && !hit; ++i) {
                    double px = box[0] + (x + (i + 0.5) / ss) * dx;
                    double py = box[1] + (y + (j + 0.5) / ss) * dy;
                    for (int k = 0; k < np; ++k)
                        if (point_in_poly(polys[k], px, py)) { hit = 1; break; }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return -2; }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(buf, 1, (size_t)w * (size_t)h, f);
    fclose(f);
    free(buf);
    return 0;
}

/* ---------------- 反解求解器 ---------------- */

/* 目标窗口的外接圆：顶点均值为心、平均半径为 r。
 * 对正多边形与参考圆窗口这是精确外接圆，候选窗口由此定义。 */
static int circumcircle(const FPoly *t, double *cx, double *cy, double *r) {
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < t->n; ++i) { sx += t->pts[i].x; sy += t->pts[i].y; }
    sx /= t->n; sy /= t->n;
    double sr = 0.0;
    for (int i = 0; i < t->n; ++i)
        sr += hypot(t->pts[i].x - sx, t->pts[i].y - sy);
    sr /= t->n;
    if (!(sr > 0.0)) return -6;
    *cx = sx; *cy = sy; *r = sr;
    return 0;
}

const char *fit_strerror(int code) {
    switch (code) {
        case 0:  return "ok";
        case -1: return "null argument";
        case -2: return "subject or target has fewer than 3 vertices (rejected)";
        case -3: return "invalid radius or degenerate window";
        case -4: return "vertex count exceeds capacity";
        case -5: return "orientation samples must be >= 4";
        case -6: return "target clip area is not positive";
        default: return "unknown error";
    }
}

int fit_solve(const FPoly *subj, const FPoly *target, int nmax, int ntheta,
              double tol, double gain_eps, FitReport *rep) {
    if (!subj || !target || !rep) return -1;
    if (subj->n < 3) return -2;
    if (target->n < 3) return -3;
    if (nmax < 3) return -2;
    if (nmax > FIT_MAX) return -4;
    if (ntheta < 4) return -5;
    if (!(tol > 0.0)) tol = FIT_TOL;
    if (!(gain_eps > 0.0)) gain_eps = FIT_GAIN;
    memset(rep, 0, sizeof(*rep));
    rep->tol = tol;

    double cx, cy, r;
    int rc = circumcircle(target, &cx, &cy, &r);
    if (rc) return rc;

    FPoly tclip;
    rc = fit_clip(subj, target, &tclip);
    if (rc) return rc;
    double tarea = fit_area(&tclip);
    if (!(tarea > 0.0)) return -6;
    rep->target_area = tarea;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int stop = -1, lookahead = 0, stall = 0;
    int chosen_locked = 0;
    double prev = -1.0;
    for (int n = 3; n <= nmax; ++n) {
        double best = 1.0 / 0.0, bestth = 0.0, bestarea = 0.0;
        double sector = 2.0 * M_PI / n;
        for (int j = 0; j < ntheta; ++j) {
            double th = sector * j / ntheta;
            FPoly win, clipped;
            if (fit_regular(n, cx, cy, r, th, &win)) continue;
            if (fit_clip(subj, &win, &clipped)) continue;
            double a = fit_area(&clipped);
            double e = fabs(a - tarea) / tarea;
            if (e < best) { best = e; bestth = th; bestarea = a; }
        }
        /* 取到最小误差的窗口再做一次共线检测，单独写入该行
         * （正多边形恒为 0；一旦候选窗口退化，共线顶点在此显形） */
        FPoly bestwin;
        fit_regular(n, cx, cy, r, bestth, &bestwin);
        FitRow *row = &rep->rows[rep->rows_n++];
        row->n = n; row->err = best; row->theta = bestth;
        row->area = bestarea;
        row->collinear = fit_collinear(&bestwin, NULL, 0);

        /* 选定值在触发停止的那一刻锁定：
         * 容差停止=首个过线 n；精确停止=全局最优（零误差）n */
        if (!chosen_locked) {
            rep->chosen = n; rep->chosen_err = best; rep->chosen_theta = bestth;
        }

        if (stop < 0) {
            if (best <= FIT_EPS) {
                stop = FIT_STOP_EXACT; lookahead = 3; chosen_locked = 1;
            } else if (best <= tol) {
                stop = FIT_STOP_TOL; lookahead = 3; chosen_locked = 1;
            } else if (prev >= 0.0) {
                double rel = (prev - best) / prev;
                if (rel >= 0.0 && rel < gain_eps) {
                    if (++stall >= 3) {
                        stop = FIT_STOP_GAIN; lookahead = 1; chosen_locked = 1;
                    }
                } else stall = 0;
            }
        } else if (--lookahead <= 0) break;
        prev = best;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    rep->ms = (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    if (stop < 0) {
        rep->stop_code = FIT_STOP_CAP;
        rep->stop_reason = "hit vertex cap without reaching tolerance";
        /* cap 情况下取全程误差最小的 n */
        double best = 1.0 / 0.0; int bi = 0;
        for (int i = 0; i < rep->rows_n; ++i)
            if (rep->rows[i].err < best) { best = rep->rows[i].err; bi = i; }
        rep->chosen = rep->rows[bi].n;
        rep->chosen_err = rep->rows[bi].err;
        rep->chosen_theta = rep->rows[bi].theta;
    } else {
        rep->stop_code = stop;
        rep->stop_reason =
            stop == FIT_STOP_EXACT ? "zero-error floor reached (vertex count matches target); more vertices only raise the error" :
            stop == FIT_STOP_TOL   ? "first n inside area tolerance; O(1/n^2) convergence makes extra vertices sub-threshold polish" :
            stop == FIT_STOP_GAIN  ? "relative gains below gain threshold for 3 steps: improvement exhausted" :
                                     "stopped";
    }
    return 0;
}
