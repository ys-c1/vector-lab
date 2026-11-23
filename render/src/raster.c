#include "raster.h"
#include <string.h>

/* ---- 基线 ---- */
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

/* ---- 确定性：固定求值顺序、禁用乘加合并 ----
 * 每个复合表达式拆成具名临时量，严格串行；比较用临时量，
 * 保证跨边循环内结果只取决于输入，不取决于编译器的重排/融合。
 * 禁用合并由构建标志 -ffp-contract=off 保证（见 Makefile DET_CFLAGS）。
 */
static int inside_fixed(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i];
        RPoint b = p->pts[(i + 1) % p->n];

        int cay = a.y > y;
        int cby = b.y > y;
        if (cay == cby) continue;

        double num = y - a.y;          /* 分子 */
        double den = b.y - a.y;        /* 分母 */
        double t = num / den;          /* 交点参数 */

        double dx = b.x - a.x;         /* 边的 x 方向差 */
        double off = dx * t;           /* 交点相对 a.x 的偏移（独立乘，不合入加法） */
        double crossx = a.x + off;     /* 交点 x */
        if (crossx > x) ++c;
    }
    return c & 1;
}

/* 固定顺序的采样点坐标，避免 (x + (i+0.5)/ss) * dx 这类可被重排的复合式。 */
static void sample_fixed(const RBox *box, int x, int y, int i, int j, int ss,
                         double dx, double dy, double *px, double *py) {
    double half_i = (double)i + 0.5;
    double frac_i = half_i / (double)ss;
    double cell_x = (double)x + frac_i;
    double span_x = cell_x * dx;
    *px = box->minx + span_x;

    double half_j = (double)j + 0.5;
    double frac_j = half_j / (double)ss;
    double cell_y = (double)y + frac_j;
    double span_y = cell_y * dy;
    *py = box->miny + span_y;
}

static int fill_common(const RScene *s, const RBox *box, int w, int h, int ss,
                       int fixed, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double px, py;
                    if (fixed) {
                        sample_fixed(box, x, y, i, j, ss, dx, dy, &px, &py);
                    } else {
                        px = box->minx + (x + (i + 0.5) / ss) * dx;
                        py = box->miny + (y + (j + 0.5) / ss) * dy;
                    }
                    for (int k = 0; k < s->npolys; ++k) {
                        int in = fixed ? inside_fixed(&s->polys[k], px, py)
                                       : inside(&s->polys[k], px, py);
                        if (in) { hit = 1; break; }
                    }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
    return 0;
}

int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf) {
    return fill_common(s, box, w, h, ss, 0, buf);
}

int raster_fill_cfg(const RScene *s, const RBox *box, int w, int h, int ss,
                    int fixed, unsigned char *buf) {
    return fill_common(s, box, w, h, ss, fixed, buf);
}
