#define _POSIX_C_SOURCE 199309L
#include "dash.h"
#include <math.h>
#include <string.h>
#include <time.h>

int dash_polyline(const DPoint *pts, int n, double on, double off, DPoly *out) {
    if (!pts || !out || n < 2 || on <= 0.0 || off <= 0.0) return -1;
    double period = on + off;
    double travelled = 0.0;
    int k = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) return -2;
        for (double s = 0.0; s < len; s += 0.5) {
            double t = travelled + s;
            double ph = fmod(t, period);
            if (ph <= on && k < 126) {
                out->pts[k].x = pts[i].x + dx * (s / len);
                out->pts[k].y = pts[i].y + dy * (s / len);
                ++k;
            }
        }
        travelled += len;
    }
    out->count = k;
    return 0;
}

static double now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

int dash_solve_period(double total, double on, double off, DashFit *fit) {
    if (!fit) return -1;
    double t0 = now_us();
    memset(fit, 0, sizeof *fit);
    double period = on + off;
    if (!(period > 0.0)) { fit->elapsed_us = now_us() - t0; return -3; } /* 周期为零 */
    if (on <= 0.0 || off <= 0.0) { fit->elapsed_us = now_us() - t0; return -1; }
    if (!(total >= period)) { fit->elapsed_us = now_us() - t0; return -4; } /* 不足一个周期 */
    /* 目标: 终点落在末段实线中点, 即 total = (m + on/(2*period)) * solved。
     * m 取最接近的整数周期数, 使 solved 相对手调周期微调最小。 */
    double half = on / (2.0 * period);
    long m = lround(total / period - half);
    if (m < 1) m = 1;
    double solved = total / ((double)m + half);
    double scale = solved / period;
    fit->period = solved;
    fit->on = on * scale;
    fit->off = off * scale;
    fit->cycles = (int)m;
    fit->end_phase = total - (double)m * solved;
    fit->end_pos = fit->end_phase / fit->on;
    double r = fit->end_phase;
    fit->end_dev = r < 0.0 ? -r : (r > fit->on ? r - fit->on : 0.0);
    fit->elapsed_us = now_us() - t0;
    return 0;
}

int dash_polyline_fit(const DPoint *pts, int n, double on, double off,
                      int fit_enabled, DashFit *fit, DPoly *out) {
    if (!fit_enabled) return dash_polyline(pts, n, on, off, out);
    if (!pts || !out || n < 2 || on <= 0.0 || off <= 0.0) return -1;
    double total = 0.0;
    int degen = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) ++degen; else total += len;
    }
    DashFit f;
    int rc = dash_solve_period(total, on, off, &f);
    f.degenerate = degen;
    if (fit) *fit = f;
    if (rc != 0) { out->count = 0; return rc; }
    double travelled = 0.0;
    int k = 0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) continue;
        for (double s = 0.0; s < len; s += 0.5) {
            double t = travelled + s;
            double ph = fmod(t, f.period);
            if (ph <= f.on && k < 126) {
                out->pts[k].x = pts[i].x + dx * (s / len);
                out->pts[k].y = pts[i].y + dy * (s / len);
                ++k;
            }
        }
        travelled += len;
    }
    out->count = k;
    return 0;
}
