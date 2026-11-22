#define _POSIX_C_SOURCE 199309L
#include "adaptive.h"
#include <float.h>
#include <math.h>
#include <time.h>

#define EPS_LEN 1e-12
#define RATIO_MAX 1.2
#define MAXPTS 128

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

int stroke_curvature(const SPoint *pts, int n, double *kappa, int *flags) {
    if (!pts || !kappa || n < 2 || n > MAXPTS) return -1;
    double kv[MAXPTS];
    int inf[MAXPTS];
    kv[0] = 0.0; inf[0] = 0;
    for (int j = 1; j + 1 < n; ++j) {
        double e0x = pts[j].x - pts[j-1].x, e0y = pts[j].y - pts[j-1].y;
        double e1x = pts[j+1].x - pts[j].x, e1y = pts[j+1].y - pts[j].y;
        double l0 = sqrt(e0x*e0x + e0y*e0y), l1 = sqrt(e1x*e1x + e1y*e1y);
        double cross = e0x*e1y - e0y*e1x, dot = e0x*e1x + e0y*e1y;
        if (l0 < EPS_LEN || l1 < EPS_LEN) {
            kv[j] = HUGE_VAL; inf[j] = 1;              /* 零长边 */
        } else if (dot < 0.0 && fabs(cross) <= 1e-9 * l0 * l1) {
            kv[j] = HUGE_VAL; inf[j] = 1;              /* 180° 回折 */
        } else {
            kv[j] = 2.0 * atan2(cross, dot) / (l0 + l1);
            inf[j] = 0;
        }
    }
    kv[n-1] = 0.0; inf[n-1] = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        double len = sqrt(dx*dx + dy*dy);
        if (len < EPS_LEN || inf[i] || inf[i+1]) {
            kappa[i] = HUGE_VAL;
            if (flags) flags[i] = SCURV_INF;
        } else {
            kappa[i] = 0.5 * (kv[i] + kv[i+1]);
            if (flags) flags[i] = (kappa[i] == 0.0) ? SCURV_ZERO : SCURV_OK;
        }
    }
    return 0;
}

/* 视觉粗细模型：w_app = w / (1 - (k*w/2)^2)，对 w 有解析逆：
 * w = 2*t / (1 + sqrt(1 + (k*t)^2))，k=0 时退化为 t。 */
int stroke_widths_solve(const SPoint *pts, int n, double target_width,
                        double *widths, SWidthReport *rep) {
    if (!pts || !widths || n < 2 || n - 1 > STROKE_MAX_SEG || !(target_width > 0.0))
        return -1;
    double t0 = now_sec();
    double kappa[STROKE_MAX_SEG];
    int flags[STROKE_MAX_SEG];
    int rc = stroke_curvature(pts, n, kappa, flags);
    if (rc != 0) return rc;
    int nseg = n - 1;
    double lo = target_width / sqrt(RATIO_MAX);
    double hi = target_width * sqrt(RATIO_MAX);
    double mn = DBL_MAX, mx = 0.0, maxdev = 0.0;
    for (int i = 0; i < nseg; ++i) {
        double k = fabs(kappa[i]);
        double w;
        if (isinf(k)) {
            w = lo;
        } else {
            w = 2.0 * target_width / (1.0 + sqrt(1.0 + k * k * target_width * target_width));
            if (w < lo) w = lo;
            if (w > hi) w = hi;
        }
        widths[i] = w;
        double dev = NAN;
        if (!isinf(k)) {
            double u = 0.5 * k * w;
            double apparent = w / (1.0 - u * u);
            dev = (apparent - target_width) / target_width;
            if (fabs(dev) > maxdev) maxdev = fabs(dev);
        }
        if (w < mn) mn = w;
        if (w > mx) mx = w;
        if (rep) {
            rep->seg[i].kappa = kappa[i];
            rep->seg[i].width = w;
            rep->seg[i].deviation = dev;
            rep->seg[i].flag = flags[i];
        }
    }
    if (rep) {
        rep->nseg = nseg;
        rep->min_width = mn;
        rep->max_width = mx;
        rep->max_abs_deviation = maxdev;
        rep->seconds = now_sec() - t0;
    }
    return 0;
}

int stroke_polyline_varying(const SPoint *pts, int n, const double *widths,
                            SPoly *out, int *err_seg) {
    if (!pts || !widths || !out || n < 2 || n - 1 > STROKE_MAX_SEG) return -1;
    int nseg = n - 1;
    for (int i = 0; i < nseg; ++i) {
        if (!(widths[i] > 0.0)) {
            if (err_seg) *err_seg = i;
            return -3;
        }
    }
    int k = 0;
    for (int i = 0; i < n && k < 126; ++i) {
        double dx, dy, w;
        if (i + 1 < n) { dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y; }
        else { dx = pts[i].x - pts[i-1].x; dy = pts[i].y - pts[i-1].y; }
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) return -2;
        if (i == 0) w = widths[0];
        else if (i == n - 1) w = widths[nseg - 1];
        else w = 0.5 * (widths[i-1] + widths[i]);
        double h = w * 0.5;
        double nx = -dy / len * h, ny = dx / len * h;
        out->pts[k].x = pts[i].x + nx; out->pts[k].y = pts[i].y + ny; ++k;
        out->pts[k].x = pts[i].x - nx; out->pts[k].y = pts[i].y - ny; ++k;
    }
    out->count = k;
    return 0;
}

int stroke_outline(const SPoint *pts, int n, double width, int adaptive,
                   SPoly *out, SWidthReport *rep) {
    if (!adaptive) return stroke_polyline(pts, n, width, out);
    double widths[STROKE_MAX_SEG];
    int rc = stroke_widths_solve(pts, n, width, widths, rep);
    if (rc != 0) return rc;
    return stroke_polyline_varying(pts, n, widths, out, NULL);
}
