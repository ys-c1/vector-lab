#include "dash.h"
#include <math.h>

int dash_polyline(const DPoint *pts, int n, double on, double off, DPoly *out) {
    return dash_polyline_phase(pts, n, on, off, 0.0, out);
}

int dash_solve_phase(const DPoint *pts, int n, double on, double off,
                     DashAlign *al) {
    if (!pts || !al || n < 2 || on <= 0.0 || off <= 0.0) return -1;
    double period = on + off;
    if (!(period > 0.0)) return -1;          /* zero / non-finite period */
    double total = 0.0;
    for (int i = 0; i + 1 < n; ++i) {
        double dx = pts[i+1].x - pts[i].x, dy = pts[i+1].y - pts[i].y;
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) return -2;           /* degenerate zero-length */
        total += len;
    }
    /* Endpoint arc position within the pattern: r in [0, period). */
    double r = fmod(total, period);
    double phase, endoff;
    if (r <= on) {
        /* Endpoint already on solid: no shift needed. */
        phase = 0.0;
        endoff = 0.0;
    } else if (period - r <= on) {
        /* Shift so the endpoint wraps to the start of the next on-run;
         * the start then sits at period - r, which is still on solid. */
        phase = period - r;
        endoff = 0.0;
    } else {
        /* No shift puts both endpoints exactly on solid (gap wider than
         * the on-run). Best compromise: split the misalignment evenly,
         * each endpoint intrudes (r - on)/2 into the gap. Reject when
         * that exceeds half a dash segment. */
        endoff = (r - on) / 2.0;
        if (endoff > on / 2.0) return -3;    /* phase out of range */
        phase = period - endoff;
    }
    if (!(phase >= 0.0 && phase < period)) return -3;
    al->total_len = total;
    al->period = period;
    al->phase = phase;
    al->end_offset = endoff;
    return 0;
}

int dash_polyline_phase(const DPoint *pts, int n, double on, double off,
                        double phase, DPoly *out) {
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
            double ph = fmod(t + phase, period);
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
