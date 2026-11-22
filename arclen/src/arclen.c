#include "arclen.h"
#include <math.h>
#include <stdlib.h>

static double seg_len(APoint a, APoint b) {
    double dx = b.x - a.x, dy = b.y - a.y;
    return sqrt(dx * dx + dy * dy);
}

int arclen_build(const APoint *pts, int n, ATable *out) {
    if (!pts || !out || n < 2 || n > 64) return -1;
    out->count = n;
    out->s[0] = 0.0;
    for (int i = 1; i < n; ++i)
        out->s[i] = out->s[i-1] + seg_len(pts[i-1], pts[i]);
    return 0;
}

double arclen_total(const ATable *t, const APoint *pts, int n) {
    if (!t || !pts || n < 2) return -1.0;
    (void)pts;
    return t->s[n-1];
}

int arclen_sample_at(const APoint *pts, int n, const ATable *t, double s, APoint *out) {
    if (!pts || !t || !out || n < 2) return -1;
    if (s < 0.0 || s > t->s[n-1]) return -2;
    int i = 0;
    while (i + 2 < n && t->s[i+1] < s) ++i;
    double span = t->s[i+1] - t->s[i];
    double f = span > 1e-12 ? (s - t->s[i]) / span : 0.0;
    out->x = pts[i].x + (pts[i+1].x - pts[i].x) * f;
    out->y = pts[i].y + (pts[i+1].y - pts[i].y) * f;
    return 0;
}

int arclen_even(const APoint *pts, int n, double spacing, APoint *out, int maxout) {
    if (!pts || n < 2 || !(spacing > 0.0)) return ARCLEN_ERR_ARG;
    double *s = (double *)malloc((size_t)n * sizeof(double));
    if (!s) return ARCLEN_ERR_ARG;
    s[0] = 0.0;
    for (int i = 1; i < n; ++i)
        s[i] = s[i-1] + seg_len(pts[i-1], pts[i]);
    double total = s[n-1];
    int rc;
    if (total == 0.0) {
        rc = ARCLEN_ERR_TOTAL_ZERO;
    } else if (spacing > total) {
        rc = ARCLEN_ERR_SPACING;
    } else {
        int nseg = (int)floor(total / spacing + 1e-9);
        if (nseg < 2) {
            rc = ARCLEN_ERR_SEGCOUNT;
        } else if (!out) {
            rc = nseg + 1;
        } else if (nseg + 1 > maxout) {
            rc = ARCLEN_ERR_CAP;
        } else {
            int i = 0;
            for (int k = 0; k <= nseg; ++k) {
                double target = spacing * k;
                while (i + 2 < n && s[i+1] < target) ++i;
                double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
                double len = sqrt(dx * dx + dy * dy);
                double f = len > 1e-12 ? (target - s[i]) / len : 0.0;
                out[k].x = pts[i].x + dx * f;
                out[k].y = pts[i].y + dy * f;
            }
            rc = nseg + 1;
        }
    }
    free(s);
    return rc;
}

const char *arclen_strerror(int code) {
    switch (code) {
        case ARCLEN_ERR_ARG: return "参数非法";
        case ARCLEN_ERR_TOTAL_ZERO: return "总长为零";
        case ARCLEN_ERR_SPACING: return "间距比总长还大";
        case ARCLEN_ERR_SEGCOUNT: return "段数小于二";
        case ARCLEN_ERR_CAP: return "输出容量不足";
        default: return "未知错误";
    }
}
