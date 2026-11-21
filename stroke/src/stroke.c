#include "stroke.h"
#include <math.h>

int stroke_polyline(const SPoint *pts, int n, double width, SPoly *out) {
    if (!pts || !out || n < 2 || width <= 0.0) return -1;
    double h = width * 0.5;
    int k = 0;
    for (int i = 0; i < n && k < 126; ++i) {
        double dx = 0.0, dy = 0.0;
        if (i + 1 < n) { dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y; }
        else { dx = pts[i].x - pts[i-1].x; dy = pts[i].y - pts[i-1].y; }
        double len = sqrt(dx*dx + dy*dy);
        if (len < 1e-9) return -2;
        double nx = -dy / len * h, ny = dx / len * h;
        out->pts[k].x = pts[i].x + nx; out->pts[k].y = pts[i].y + ny; ++k;
        out->pts[k].x = pts[i].x - nx; out->pts[k].y = pts[i].y - ny; ++k;
    }
    out->count = k;
    return 0;
}
