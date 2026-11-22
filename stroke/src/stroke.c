#include "stroke.h"
#include <math.h>

#define STROKE_MIN_LEN     1e-9
#define STROKE_NORMAL_EPS  1e-12

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

int stroke_polyline_xform(const SPoint *pts, int n, double width,
                          const XForm *xform, SPoly *out, int *err_seg) {
    if (err_seg) *err_seg = -1;
    if (!pts || !out || n < 2 || width <= 0.0) return STROKE_ERR_ARG;
    if (!xform) return stroke_polyline(pts, n, width, out);

    double h = width * 0.5;
    double det = xform_det(*xform);
    if (!isfinite(det) || fabs(det) < XFORM_DET_EPS) return STROKE_ERR_SINGULAR;

    int k = 0;
    for (int i = 0; i < n && k < 126; ++i) {
        double dx = 0.0, dy = 0.0;
        if (i + 1 < n) { dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y; }
        else { dx = pts[i].x - pts[i-1].x; dy = pts[i].y - pts[i-1].y; }
        double len = sqrt(dx*dx + dy*dy);
        if (len < STROKE_MIN_LEN) { if (err_seg) *err_seg = i; return STROKE_ERR_ZERO_SEG; }

        /* Unit normal of the input-space centreline; the inverse transpose
           carries it to a direction that stays perpendicular in output space. */
        double nx = -dy / len, ny = dx / len;
        XPoint c = xform_apply(*xform, (XPoint){pts[i].x, pts[i].y});
        double ox = 0.0, oy = 0.0;
        int rc = xform_inverse_transpose(*xform, nx, ny, &ox, &oy);
        if (rc != 0) return STROKE_ERR_SINGULAR;

        double norm = sqrt(ox*ox + oy*oy);
        if (!isfinite(norm) || norm < STROKE_NORMAL_EPS) {
            if (err_seg) *err_seg = i;
            return STROKE_ERR_DEGEN_NORMAL;
        }
        /* Offset magnitude h in output space: the inverse transpose only
           supplies the direction, so the half-width is uniform on both sides. */
        double ux = ox / norm * h, uy = oy / norm * h;
        out->pts[k].x = c.x + ux; out->pts[k].y = c.y + uy; ++k;
        out->pts[k].x = c.x - ux; out->pts[k].y = c.y - uy; ++k;
    }
    out->count = k;
    return STROKE_OK;
}
