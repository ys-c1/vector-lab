#include "winrot.h"
#include <math.h>
#include <string.h>

#define WINROT_PI 3.14159265358979323846

static int finite_pos(double v) { return isfinite(v) && v > 0.0; }

static double left_root(double w1, double w2, double bound) {
    /* w1*cos(t) + w2*sin(t) = bound 的左侧分支根；无约束返回 INFINITY。 */
    double R = hypot(w1, w2);
    if (R <= bound) return INFINITY;
    double phi = atan2(w2, w1);
    double c = bound / R;
    if (c > 1.0) c = 1.0;
    if (c < -1.0) c = -1.0;
    double t = phi - acos(c);
    return t * 180.0 / WINROT_PI;
}

int winrot_solve(double bw, double bh, double win_w, double win_h,
                 double cap_deg, WinRotSolve *out) {
    if (!out) return WINROT_BADARG;
    if (!isfinite(bw) || !isfinite(bh) || !finite_pos(win_w) ||
        !finite_pos(win_h) || !isfinite(cap_deg) || !(cap_deg > 0.0) ||
        !(cap_deg <= 90.0) || bw < 0.0 || bh < 0.0)
        return WINROT_BADARG;

    out->bw = bw;
    out->bh = bh;
    out->win_w = win_w;
    out->win_h = win_h;
    out->cap_deg = cap_deg;
    out->theta_deg = INFINITY;
    out->theta_width_deg = INFINITY;
    out->theta_height_deg = INFINITY;
    out->binding = WINROT_BIND_NONE;

    if (bw == 0.0 && bh == 0.0) return WINROT_DEGEN_POINT;
    if (bw == 0.0 || bh == 0.0) return WINROT_DEGEN_LINE;
    if (win_w > bw || win_h > bh) return WINROT_NOFIT;

    /* 窗口任性摆放时空间无限：任一方向都能把窗口对到包围盒的对角线上。
     * 真正的零空角最大角是两族约束根里的较大者；所以“无约束”不是没有上限，
     * 而是先按 90 度封顶，再和另一条约束取小的。 */
    double tw = left_root(win_w, win_h, bw);
    double th = left_root(win_h, win_w, bh);
    if (tw == INFINITY) tw = 90.0;
    if (th == INFINITY) th = 90.0;
    out->theta_width_deg = tw;
    out->theta_height_deg = th;

    double t = tw;
    int binding = WINROT_BIND_WIDTH;
    if (th < t) {
        t = th;
        binding = WINROT_BIND_HEIGHT;
    }
    if (tw >= 90.0 && th >= 90.0) binding = WINROT_BIND_NONE;

    out->theta_deg = t;
    out->binding = binding;
    if (t > cap_deg) return WINROT_OVER_CAP;
    return WINROT_OK;
}

const char *winrot_status_str(int status) {
    switch (status) {
    case WINROT_OK: return "ok";
    case WINROT_BADARG: return "badarg";
    case WINROT_DEGEN_POINT: return "degenerate point";
    case WINROT_DEGEN_LINE: return "degenerate line";
    case WINROT_NOFIT: return "window does not fit at zero rotation";
    case WINROT_OVER_CAP: return "exceeds angle cap";
    default: return "unknown";
    }
}

const char *winrot_binding_str(int binding) {
    switch (binding) {
    case WINROT_BIND_WIDTH: return "width";
    case WINROT_BIND_HEIGHT: return "height";
    default: return "none";
    }
}

static int inside(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

int winrot_render(const RScene *s, double cx, double cy, double win_w,
                  double win_h, double theta_deg, int w, int h, int ss,
                  unsigned char *buf) {
    if (!s || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (!finite_pos(win_w) || !finite_pos(win_h)) return -1;
    if (!isfinite(cx) || !isfinite(cy) || !isfinite(theta_deg)) return -1;

    double th = theta_deg * WINROT_PI / 180.0;
    double c = cos(th), sn = sin(th);
    double dxs = win_w / w, dys = win_h / h;
    memset(buf, 0, (size_t)w * (size_t)h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss && !hit; ++j)
                for (int i = 0; i < ss && !hit; ++i) {
                    double u = (x + (i + 0.5) / ss) * dxs - win_w * 0.5;
                    double v = (y + (j + 0.5) / ss) * dys - win_h * 0.5;
                    double px = cx + u * c - v * sn;
                    double py = cy + u * sn + v * c;
                    for (int k = 0; k < s->npolys; ++k)
                        if (inside(&s->polys[k], px, py)) { hit = 1; break; }
                }
            buf[y * w + x] = (unsigned char)(hit ? 255 : 0);
        }
    return 0;
}

int winrot_corner_box(int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    int short_side = w < h ? w : h;
    int k = short_side / 8;
    if (k < 1) k = 1;
    int half = short_side / 2;
    if (k > half) k = half;
    if (k < 1) k = 1;
    return k;
}

static int in_corner(int x, int y, int w, int h, int k) {
    if (!((x < k || x >= w - k) && (y < k || y >= h - k))) return 0;
    return 1;
}

int winrot_count_corner_empty(const unsigned char *buf, int w, int h) {
    if (!buf || w <= 0 || h <= 0) return -1;
    int k = winrot_corner_box(w, h), n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (in_corner(x, y, w, h, k) && buf[y * w + x] == 0) ++n;
    return n;
}

int winrot_count_empty(const unsigned char *buf, int w, int h) {
    if (!buf || w <= 0 || h <= 0) return -1;
    int n = 0;
    for (int i = 0; i < w * h; ++i)
        if (buf[i] == 0) ++n;
    return n;
}
