#define _POSIX_C_SOURCE 199309L
#include "stroke.h"
#include <math.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define S_EPS 1e-9

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

const char *stroke_join_name(SJoin j) {
    switch (j) {
    case SJOIN_MITER: return "miter";
    case SJOIN_BEVEL: return "bevel";
    case SJOIN_ROUND: return "round";
    }
    return "?";
}

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* 三种接法相对理想扇形（半径 h、张角 alpha）的面积误差。
 * 理想轮廓在拐角外侧是圆弧扇形 sector = h^2*alpha/2：
 *   斜切 = 内接三角形（削掉弓形）；
 *   斜接 = 三角形加尖峰（鼓起）；
 *   圆角 = segs 段弦对扇形的多边形逼近。
 */
static void join_errors(double h, double alpha, int segs,
                        double *em, double *eb, double *er) {
    double sector = 0.5 * h * h * alpha;
    double bevel  = 0.5 * h * h * sin(alpha);
    double sa = sin(alpha * 0.5);
    double miter  = bevel + h * h * sa * sa * tan(alpha * 0.5);
    double round_ = 0.5 * h * h * (double)segs * sin(alpha / (double)segs);
    *em = fabs(miter - sector);
    *eb = fabs(bevel - sector);
    *er = fabs(round_ - sector);
}

/* 取误差最小者；误差完全相等时固定选斜切：顶点最少、计算最简、结果确定。 */
static SJoin pick_join(double em, double eb, double er) {
    if (eb <= em && eb <= er) return SJOIN_BEVEL;
    if (em <= er) return SJOIN_MITER;
    return SJOIN_ROUND;
}

static int stroke_adaptive(const SPoint *pts, int n, double width,
                           double err_max, int segs,
                           SJoinReport *reports, SPoly *out) {
    double h = width * 0.5;
    SPoint L[128], R[128];
    int lc = 0, rc = 0;
    double dx = pts[1].x - pts[0].x, dy = pts[1].y - pts[0].y;
    double len = sqrt(dx*dx + dy*dy);
    if (len < S_EPS) return -2;
    double pdx = dx / len, pdy = dy / len;      /* 上一段单位方向 */
    double pnx = -pdy, pny = pdx;               /* 上一段单位左法向 */
    L[lc++] = (SPoint){ pts[0].x + pnx*h, pts[0].y + pny*h };
    R[rc++] = (SPoint){ pts[0].x - pnx*h, pts[0].y - pny*h };
    double nx = pnx, ny = pny;

    for (int i = 1; i < n - 1; ++i) {
        dx = pts[i+1].x - pts[i].x; dy = pts[i+1].y - pts[i].y;
        len = sqrt(dx*dx + dy*dy);
        if (len < S_EPS) return -2;
        double cdx = dx / len, cdy = dy / len;
        nx = -cdy; ny = cdx;
        double cross = pdx*cdy - pdy*cdx;
        double dot   = pdx*cdx + pdy*cdy;
        double alpha = atan2(cross, dot);       /* 有符号转角 */
        if (fabs(alpha) < S_EPS) return -3;              /* 0 度：方向未改变，无角可接 */
        if (fabs(M_PI - fabs(alpha)) < S_EPS) return -4; /* 180 度：折返，接法未定义 */
        double a = fabs(alpha);

        double t0 = now_sec();
        double em, eb, er;
        join_errors(h, a, segs, &em, &eb, &er);
        SJoin j = pick_join(em, eb, er);
        double t1 = now_sec();
        double err = (j == SJOIN_MITER) ? em : (j == SJOIN_BEVEL) ? eb : er;
        if (reports) {
            SJoinReport *r = &reports[i-1];
            r->vertex = i; r->angle = a; r->join = j;
            r->err_miter = em; r->err_bevel = eb; r->err_round = er;
            r->err = err; r->seconds = t1 - t0;
        }
        if (!(err < err_max)) return -5;

        /* 外侧在转角张开的一侧：左转（cross>0）时外侧在右 */
        int outer_left = (cross < 0.0);
        double on1x = outer_left ? pnx : -pnx, on1y = outer_left ? pny : -pny;
        double on2x = outer_left ? nx  : -nx,  on2y = outer_left ? ny  : -ny;
        /* 内侧取两条偏移线的交点，不产生面积误差 */
        double in1x = -on1x, in1y = -on1y, in2x = -on2x, in2y = -on2y;
        double wx = h*(in2x - in1x), wy = h*(in2y - in1y);
        double t = (wx*cdy - wy*cdx) / cross;
        SPoint inner = { pts[i].x + in1x*h + t*pdx, pts[i].y + in1y*h + t*pdy };

        SPoint jp[66]; int jc = 0;
        SPoint A = { pts[i].x + on1x*h, pts[i].y + on1y*h };
        SPoint B = { pts[i].x + on2x*h, pts[i].y + on2y*h };
        if (j == SJOIN_BEVEL) {
            jp[jc++] = A; jp[jc++] = B;
        } else if (j == SJOIN_MITER) {
            double bx = on1x + on2x, by = on1y + on2y;
            double bl = sqrt(bx*bx + by*by);
            double ml = h / cos(a * 0.5);
            jp[jc++] = A;
            jp[jc++] = (SPoint){ pts[i].x + bx/bl*ml, pts[i].y + by/bl*ml };
            jp[jc++] = B;
        } else {
            double th0 = atan2(on1y, on1x);
            double sweep = atan2(on1x*on2y - on1y*on2x, on1x*on2x + on1y*on2y);
            for (int s = 0; s <= segs; ++s) {
                double th = th0 + sweep * (double)s / (double)segs;
                jp[jc++] = (SPoint){ pts[i].x + h*cos(th), pts[i].y + h*sin(th) };
            }
        }
        if (lc + rc + jc + 2 > 128) return -6;
        if (outer_left) {
            for (int s = 0; s < jc; ++s) L[lc++] = jp[s];
            R[rc++] = inner;
        } else {
            for (int s = 0; s < jc; ++s) R[rc++] = jp[s];
            L[lc++] = inner;
        }
        pdx = cdx; pdy = cdy; pnx = nx; pny = ny;
    }
    if (lc + rc + 2 > 128) return -6;
    L[lc++] = (SPoint){ pts[n-1].x + nx*h, pts[n-1].y + ny*h };
    R[rc++] = (SPoint){ pts[n-1].x - nx*h, pts[n-1].y - ny*h };

    int k = 0;
    for (int i = 0; i < lc; ++i) out->pts[k++] = L[i];
    for (int i = rc - 1; i >= 0; --i) out->pts[k++] = R[i];
    out->count = k;
    return 0;
}

int stroke_polyline_ex(const SPoint *pts, int n, double width, int adaptive,
                       double err_max, int round_segs,
                       SJoinReport *reports, SPoly *out) {
    if (!pts || !out || n < 2 || width <= 0.0) return -1;
    if (!adaptive) {
        /* 固定接法：与 stroke_polyline 逐语句一致，输出逐字节相同 */
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
    if (err_max <= 0.0 || round_segs < 1 || round_segs > 64) return -1;
    return stroke_adaptive(pts, n, width, err_max, round_segs, reports, out);
}
