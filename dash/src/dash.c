#include "dash.h"
#include <math.h>

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
