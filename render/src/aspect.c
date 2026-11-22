#include "aspect.h"
#include <math.h>
#include <stddef.h>

/* 反解：由像素宽高 w,h 与场景包围盒解出窗口四边。
 *
 * 栅格化映射（见 raster.c）：
 *   px = w / Ww * (x - minx_win)
 *   py = h / Wh * (y - miny_win)
 * 圆回到圆要求两轴“每单位长度的像素数”相等：
 *   w / Ww = h / Wh   =>   Ww / Wh = w / h        …… 纵横比方程
 * 再以“包围盒居中、至少一组边贴合”为约束，解出唯一四边：
 *   令场景宽 bw、高 bh，像素比 a = w/h。
 *   若 bw/bh >= a：窗口宽度取 bw，高度 Wh = bw / a（上下留空）；
 *   否则          窗口高度取 bh，宽度 Ww = bh * a（左右留空）。 */
int aspect_solve(const RBox *bbox, int w, int h, ASolve *out) {
    if (!bbox || !out) return -1;
    out->bad_edge = ASPECT_EDGE_NONE;
    out->degenerate = ASPECT_DEGEN_NONE;

    /* 像素宽高为零或负：拒，并指出方向 */
    if (w <= 0 || h <= 0) {
        out->bad_edge = (w <= 0 && h <= 0) ? ASPECT_EDGE_BOTH
                      : (w <= 0) ? ASPECT_EDGE_LR : ASPECT_EDGE_BT;
        return -2;
    }

    double bw = bbox->maxx - bbox->minx;
    double bh = bbox->maxy - bbox->miny;

    /* 包围盒比值为零或负：拒；点和线分开报，并指出重合的边 */
    if (bw <= 0.0 || bh <= 0.0) {
        if (bw <= 0.0 && bh <= 0.0) {
            out->degenerate = ASPECT_DEGEN_POINT;
            out->bad_edge = ASPECT_EDGE_BOTH;   /* 四边全重合：点 */
        } else if (bw <= 0.0) {
            out->degenerate = ASPECT_DEGEN_LINE;
            out->bad_edge = ASPECT_EDGE_LR;     /* 左右边重合：竖线 */
        } else {
            out->degenerate = ASPECT_DEGEN_LINE;
            out->bad_edge = ASPECT_EDGE_BT;     /* 上下边重合：横线 */
        }
        return -3;
    }

    double a = (double)w / (double)h;           /* 像素纵横比（目标窗口纵横比） */
    double cx = (bbox->minx + bbox->maxx) * 0.5;
    double cy = (bbox->miny + bbox->maxy) * 0.5;
    double Ww, Wh;
    if (bw / bh >= a) {
        Ww = bw;
        Wh = bw / a;                            /* Wh >= bh */
        out->kx = 1.0;
        out->ky = Wh / bh;
    } else {
        Wh = bh;
        Ww = bh * a;                            /* Ww >= bw */
        out->kx = Ww / bw;
        out->ky = 1.0;
    }

    out->win.minx = cx - Ww * 0.5;
    out->win.maxx = cx + Ww * 0.5;
    out->win.miny = cy - Wh * 0.5;
    out->win.maxy = cy + Wh * 0.5;
    out->ratio = Ww / Wh;
    out->px_ratio = a;
    return 0;
}

int aspect_roundness(const unsigned char *buf, int w, int h,
                     double *rx, double *ry, double *dev) {
    if (!buf || w <= 0 || h <= 0) return -1;
    long n = 0, sx = 0, sy = 0;
    size_t total = (size_t)w * (size_t)h;
    for (size_t i = 0; i < total; ++i)
        if (buf[i]) {
            sx += (long)(i % (size_t)w);
            sy += (long)(i / (size_t)w);
            ++n;
        }
    if (n < 3) return -2;                       /* 没有可测的图形 */
    double meanx = (double)sx / (double)n;
    double meany = (double)sy / (double)n;
    double vx = 0.0, vy = 0.0;
    for (size_t i = 0; i < total; ++i)
        if (buf[i]) {
            double dx = (double)(i % (size_t)w) - meanx;
            double dy = (double)(i / (size_t)w) - meany;
            vx += dx * dx;
            vy += dy * dy;
        }
    double rxx = sqrt(vx / (double)n);
    double ryy = sqrt(vy / (double)n);
    if (rxx <= 0.0 || ryy <= 0.0) return -3;
    if (rx) *rx = rxx;
    if (ry) *ry = ryy;
    if (dev) *dev = fabs(rxx - ryy) / fmin(rxx, ryy);
    return 0;
}

const char *aspect_edge_name(int edge) {
    switch (edge) {
        case ASPECT_EDGE_LR:   return "left/right";
        case ASPECT_EDGE_BT:   return "bottom/top";
        case ASPECT_EDGE_BOTH: return "all four";
        default:               return "none";
    }
}

const char *aspect_degen_name(int d) {
    switch (d) {
        case ASPECT_DEGEN_POINT: return "point";
        case ASPECT_DEGEN_LINE:  return "line";
        default:                 return "none";
    }
}
