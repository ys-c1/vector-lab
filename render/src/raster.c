#include "raster.h"
#include <stdlib.h>
#include <string.h>

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

int raster_fill(const RScene *s, const RBox *box, int w, int h, int ss, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    for (int k = 0; k < s->npolys; ++k)
                        if (inside(&s->polys[k], px, py)) { hit = 1; break; }
                }
            buf[y * w + x] = hit ? 255 : 0;
        }
    return 0;
}

/* 每条边的包围盒；边 bbox 与块矩形不相交是精确判定（纯比较，无算术误差）。 */
typedef struct { double minx, miny, maxx, maxy; } REBox;

static int point_inside_any(const RScene *s, double x, double y) {
    for (int k = 0; k < s->npolys; ++k)
        if (inside(&s->polys[k], x, y)) return 1;
    return 0;
}

/* 与 raster_fill 相同的逐像素计算，只写 [x0,x1) x [y0,y1) 区域。
 * 算式逐字保持一致，保证与整幅渲染逐字节相同。 */
static void fill_rect(const RScene *s, const RBox *box, int ss, double dx, double dy,
                      int x0, int y0, int x1, int y1, int stride, unsigned char *out) {
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    for (int k = 0; k < s->npolys; ++k)
                        if (inside(&s->polys[k], px, py)) { hit = 1; break; }
                }
            out[(y - y0) * stride + (x - x0)] = hit ? 255 : 0;
        }
}

int raster_fill_tc(const RScene *s, const RBox *box, int w, int h, int ss,
                   unsigned char *buf, TileCache *tc) {
    if (!tc) return raster_fill(s, box, w, h, ss, buf);
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (tc->tw <= 0 || tc->th <= 0) return -2; /* 图块尺寸为零不许进 */
    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;

    /* 预计算全部边的 bbox（场景上限 32 多边形 x 64 顶点） */
    REBox eb[32 * 64];
    int neb = 0;
    for (int k = 0; k < s->npolys; ++k) {
        const RPoly *p = &s->polys[k];
        for (int i = 0; i < p->n; ++i) {
            RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
            REBox *e = &eb[neb++];
            e->minx = a.x < b.x ? a.x : b.x;
            e->maxx = a.x > b.x ? a.x : b.x;
            e->miny = a.y < b.y ? a.y : b.y;
            e->maxy = a.y > b.y ? a.y : b.y;
        }
    }

    unsigned char *tmp = (unsigned char *)malloc((size_t)tc->tw * tc->th);
    if (!tmp) return -3;

    for (int ty = 0; ty * tc->th < h; ++ty)
        for (int tx = 0; tx * tc->tw < w; ++tx) {
            int x0 = tx * tc->tw, y0 = ty * tc->th;
            int x1 = x0 + tc->tw < w ? x0 + tc->tw : w;
            int y1 = y0 + tc->th < h ? y0 + tc->th : h;
            /* 覆盖全部子采样点的世界坐标矩形 */
            double wx0 = box->minx + x0 * dx, wx1 = box->minx + x1 * dx;
            double wy0 = box->miny + y0 * dy, wy1 = box->miny + y1 * dy;
            /* 分类：无边触及块矩形 => 块内每点内外状态一致，
             * 角点在任何多边形内则整块 255（FULL），否则整块 0（EMPTY）；
             * 有边触及 => MIXED，逐像素光栅化。 */
            int cls = TC_MIXED, touches = 0;
            for (int e = 0; e < neb; ++e) {
                if (eb[e].minx <= wx1 && eb[e].maxx >= wx0 &&
                    eb[e].miny <= wy1 && eb[e].maxy >= wy0) { touches = 1; break; }
            }
            if (!touches)
                cls = point_inside_any(s, wx0, wy0) ? TC_FULL : TC_EMPTY;
            int kx = cls == TC_MIXED ? tx : -1;
            int ky = cls == TC_MIXED ? ty : -1;
            const unsigned char *pix;
            int r = tc_get(tc, cls, kx, ky, &pix);
            if (r < 0) { free(tmp); return r; }
            if (!r) {
                if (cls == TC_EMPTY) memset(tmp, 0, (size_t)tc->tw * tc->th);
                else if (cls == TC_FULL) memset(tmp, 255, (size_t)tc->tw * tc->th);
                else {
                    memset(tmp, 0, (size_t)tc->tw * tc->th);
                    fill_rect(s, box, ss, dx, dy, x0, y0, x1, y1, tc->tw, tmp);
                }
                r = tc_put(tc, cls, kx, ky, tmp);
                if (r < 0) { free(tmp); return r; }
                pix = tmp;
            }
            for (int y = y0; y < y1; ++y)
                memcpy(buf + (size_t)y * w + x0,
                       pix + (size_t)(y - y0) * tc->tw, (size_t)(x1 - x0));
        }
    free(tmp);
    return 0;
}
