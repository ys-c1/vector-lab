#include "adaptive.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *adaptive_strerror(int code) {
    switch (code) {
        case AD_OK:       return "ok";
        case AD_E_ARG:    return "bad argument";
        case AD_E_EPS:    return "eps must be positive and finite";
        case AD_E_LEN:    return "piece length must be positive and finite";
        case AD_E_RADIUS: return "arc radius must be positive (line radius 0)";
        case AD_E_KIND:   return "unknown piece kind";
        case AD_E_CAP:    return "too many segments";
        default:          return "unknown error";
    }
}

static void set_err(char field[AD_FIELD_MAX], const char *name) {
    if (!field) return;
    strncpy(field, name, AD_FIELD_MAX - 1);
    field[AD_FIELD_MAX - 1] = '\0';
}

double adaptive_piece_cont(double len, double R, double eps) {
    if (!(len > 0.0) || !(R > 0.0) || !(eps > 0.0)) return 0.0;
    return len * sqrt((1.0 / R) / (8.0 * eps));
}

/*
 * 精确最少份数：等分圆心角 d = len/(nR)，每份弓高 R*(1-cos(d/2))。
 * 用 arccos 反解连续 n 作起点，再整数上下调整直到精确式成立。
 * eps >= 2R 时整个半圆及以上也能一份装下（此时 n=1），按 1 处理。
 */
int adaptive_piece_exact_n(double len, double R, double eps) {
    if (!(len > 0.0) || !(R > 0.0) || !(eps > 0.0)) return 1;
    double phi = len / R;              /* 总圆心角 */
    double d, nreal;
    if (eps >= 2.0 * R) return 1;
    d = 2.0 * acos(1.0 - eps / R);     /* 单份允许的最大圆心角 */
    if (!(d > 0.0)) return 1;
    nreal = phi / d;
    int n = (int)floor(nreal);
    if (n < 1) n = 1;
    /* 下调到真正最少 */
    while (n > 1 && R * (1.0 - cos(phi / (2.0 * (n - 1)))) <= eps) --n;
    /* 上调到满足精确式 */
    while (R * (1.0 - cos(phi / (2.0 * n))) > eps) ++n;
    return n;
}

int adaptive_plan(const AdPiece *pieces, int np, double eps,
                  AdPlan *out, int *err_piece, char err_field[AD_FIELD_MAX]) {
    if (err_piece) *err_piece = -1;
    set_err(err_field, "");
    if (!out) { set_err(err_field, "out"); return AD_E_ARG; }
    memset(out, 0, sizeof(*out));
    if (!pieces) { set_err(err_field, "pieces"); return AD_E_ARG; }
    if (np < 1 || np > AD_MAX_PIECES) {
        set_err(err_field, "np");
        return AD_E_ARG;
    }
    if (!isfinite(eps) || !(eps > 0.0)) {
        set_err(err_field, "eps");
        return AD_E_EPS;
    }

    long total = 0;
    int uniform_max = 1;
    for (int i = 0; i < np; ++i) {
        const AdPiece *p = &pieces[i];
        if (p->kind != AD_PIECE_LINE && p->kind != AD_PIECE_ARC) {
            if (err_piece) *err_piece = i;
            set_err(err_field, "kind");
            return AD_E_KIND;
        }
        if (!isfinite(p->len) || !(p->len > 0.0)) {
            if (err_piece) *err_piece = i;
            set_err(err_field, "len");
            return AD_E_LEN;
        }
        if (p->kind == AD_PIECE_ARC) {
            if (!isfinite(p->R) || !(p->R > 0.0)) {
                if (err_piece) *err_piece = i;
                set_err(err_field, "R");
                return AD_E_RADIUS;
            }
        } else if (p->R != 0.0) {
            if (err_piece) *err_piece = i;
            set_err(err_field, "R");
            return AD_E_RADIUS;
        }

        AdAlloc *a = &out->a[i];
        a->piece = i;
        a->kind = p->kind;
        a->len = p->len;
        a->R = p->kind == AD_PIECE_ARC ? p->R : 0.0;
        a->is_curve = p->kind == AD_PIECE_ARC;
        a->kappa = a->is_curve ? 1.0 / a->R : 0.0;
        /* 连续下界 c_i = len*sqrt(k/(8 eps))；先取 n_i = max(1, ceil(c_i))，
         * 再用精确弓高复核（最后一步），两者取较大者。 */
        double c = a->is_curve ? adaptive_piece_cont(a->len, a->R, eps) : 0.0;
        a->n_cont = c;
        a->n = c > 1.0 ? (int)ceil(c) : 1;
        a->dev_model = a->is_curve ? a->kappa * (a->len / a->n) * (a->len / a->n) / 8.0 : 0.0;
        if (a->is_curve) {
            double d = a->len / (a->n * a->R);
            a->dev_exact = a->R * (1.0 - cos(d * 0.5));
            a->dev_measure = adaptive_arc_dev_measure(a->R, a->len, a->n, 65);
        } else {
            a->dev_exact = 0.0;
            a->dev_measure = 0.0;
        }

        /* ceil(c_i) 是逐段硬上界下的最小整数；用精确式再确认一次，
         * 模型误差可能让 ceil(c_i) 差一，取两者较大者保证不超上界。 */
        if (a->is_curve) {
            int ne = adaptive_piece_exact_n(a->len, a->R, eps);
            if (ne > a->n) {
                a->n = ne;
                a->dev_model = a->kappa * (a->len / a->n) * (a->len / a->n) / 8.0;
                double d = a->len / (a->n * a->R);
                a->dev_exact = a->R * (1.0 - cos(d * 0.5));
                a->dev_measure = adaptive_arc_dev_measure(a->R, a->len, a->n, 65);
            }
            if (ne > uniform_max) uniform_max = ne;
        }

        if (a->is_curve) out->n_curve += a->n;
        else            out->n_line += a->n;
        total += a->n;
        if (total > 100000000L) {
            if (err_piece) *err_piece = i;
            set_err(err_field, "n");
            return AD_E_CAP;
        }
    }

    out->eps = eps;
    out->npieces = np;
    out->n_total = (int)total;
    out->uniform_per_piece = uniform_max;
    long uni = (long)uniform_max * (long)np;
    if (uni > 100000000L) { set_err(err_field, "n"); return AD_E_CAP; }
    out->uniform_total = (int)uni;
    return AD_OK;
}

double adaptive_arc_dev_measure(double R, double len, int n, int samples) {
    if (!(R > 0.0) || !(len > 0.0) || n < 1) return 0.0;
    if (samples < 3) samples = 3;
    double d = len / (double)n;        /* 每份弧长 */
    double phi = d / R;                /* 每份圆心角 */
    /* 单位圆上每份弦：起点角 0、终点角 phi；取份内样本点到弦的距离。 */
    double ax = 1.0, ay = 0.0;
    double bx = cos(phi), by = sin(phi);
    double vx = bx - ax, vy = by - ay;
    double vl = sqrt(vx * vx + vy * vy);
    double worst = 0.0;
    for (int s = 1; s < samples; ++s) {
        double t = (double)s / (double)samples;   /* 份内参数，开区间 */
        double th = phi * t;
        double px = cos(th), py = sin(th);
        double dist;
        if (vl > 0.0) {
            double w = ((px - ax) * vx + (py - ay) * vy) / (vl * vl);
            if (w < 0.0) w = 0.0;
            if (w > 1.0) w = 1.0;
            double dx = px - (ax + w * vx), dy = py - (ay + w * vy);
            dist = sqrt(dx * dx + dy * dy);
        } else {
            dist = 0.0;
        }
        if (dist > worst) worst = dist;
    }
    return worst * R;
}

int adaptive_plan_points(const AdPlan *plan, AdShapeFn shape, void *ctx,
                         AdPoint *out, int cap) {
    if (!plan || !shape || !out || plan->npieces < 1) return AD_E_ARG;
    if (cap < plan->n_total + 1) return AD_E_CAP;
    int k = 0;
    for (int i = 0; i < plan->npieces; ++i) {
        const AdAlloc *a = &plan->a[i];
        int first = (i == 0) ? 0 : 1;   /* 段间共用一个切点，不重复落点 */
        for (int j = first; j <= a->n; ++j)
            out[k++] = shape(i, a->len * (double)j / (double)a->n, ctx);
    }
    return k;
}

int adaptive_uniform_points(const AdPlan *plan, AdShapeFn shape, void *ctx,
                            AdPoint *out, int cap) {
    if (!plan || !shape || !out || plan->npieces < 1) return AD_E_ARG;
    long need = (long)plan->npieces * (long)plan->uniform_per_piece + 1;
    if ((long)cap < need) return AD_E_CAP;
    int k = 0;
    for (int i = 0; i < plan->npieces; ++i) {
        const AdAlloc *a = &plan->a[i];
        int nu = plan->uniform_per_piece;
        for (int j = (i == 0 ? 0 : 1); j <= nu; ++j) {
            double s = a->len * (double)j / (double)nu;
            out[k++] = shape(i, s, ctx);
        }
    }
    return k;
}

int adaptive_points_save(const char *path, const AdPoint *p, int n) {
    if (!path || !p || n < 0) return AD_E_ARG;
    FILE *f = fopen(path, "w");
    if (!f) return AD_E_ARG;
    fprintf(f, "%d\n", n);
    for (int i = 0; i < n; ++i) fprintf(f, "%.6f %.6f\n", p[i].x, p[i].y);
    int bad = ferror(f);
    fclose(f);
    return bad ? AD_E_ARG : AD_OK;
}

int adaptive_points_load(const char *path, AdPoint **out, int *n) {
    if (!path || !out || !n) return AD_E_ARG;
    FILE *f = fopen(path, "r");
    if (!f) return AD_E_ARG;
    int cnt = 0;
    if (fscanf(f, "%d", &cnt) != 1 || cnt < 0) { fclose(f); return AD_E_ARG; }
    AdPoint *p = (AdPoint *)malloc(sizeof(AdPoint) * (size_t)(cnt > 0 ? cnt : 1));
    if (!p) { fclose(f); return AD_E_ARG; }
    int got = 0;
    while (got < cnt && fscanf(f, "%lf %lf", &p[got].x, &p[got].y) == 2) ++got;
    fclose(f);
    if (got != cnt) { free(p); return AD_E_ARG; }
    *out = p;
    *n = cnt;
    return AD_OK;
}
