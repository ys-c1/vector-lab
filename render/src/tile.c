#include "tile.h"
#include <stdlib.h>
#include <string.h>

/* 分块边界跟踪后端。
 *
 * 与扫描线后端（raster.c）逐字节一致的关键：采样点坐标、边跨跃条件、
 * 交点参数 t 与交点 x 的浮点表达式全部照抄原 inside()，仅改变工作组织
 * 方式——分块、块内包围盒剔除、沿采样行走边生成跨、按跨推进奇偶。
 */

#define RASTER_TILE 32  /* 瓦片边长（像素） */

typedef struct { RPoint a, b; int owner; } REdge;
typedef struct { double x; int owner; } RCross;

typedef struct {
    int tile;       /* 瓦片边长 */
    int nedges;     /* 场景总边数 = 交点/边表缓冲容量 */
    RCross *cross;  /* 每行交点（带归属） */
    REdge *edges;   /* 当前瓦片的活动边表 */
    int *parity;    /* 每个多边形当前奇偶，最多 32 */
} RTileWS;

static int tile_ws_init(RTileWS *ws, int nedges, int tile) {
    if (!ws || nedges <= 0 || tile <= 0) return -1;
    ws->tile = tile;
    ws->nedges = nedges;
    ws->cross = NULL; ws->edges = NULL; ws->parity = NULL;
    ws->cross = (RCross*)malloc((size_t)nedges * sizeof(RCross));
    if (!ws->cross) goto fail;
    ws->edges = (REdge*)malloc((size_t)nedges * sizeof(REdge));
    if (!ws->edges) goto fail;
    ws->parity = (int*)calloc(32, sizeof(int));
    if (!ws->parity) goto fail;
    return 0;
fail:
    /* 初始化失败：释放已拿到的全部资源，指针复位后返回 -5。 */
    free(ws->cross); free(ws->edges); free(ws->parity);
    ws->cross = NULL; ws->edges = NULL; ws->parity = NULL;
    ws->nedges = 0;
    return -5;
}

static void tile_ws_free(RTileWS *ws) {
    if (!ws) return;
    free(ws->cross); free(ws->edges); free(ws->parity);
    ws->cross = NULL; ws->edges = NULL; ws->parity = NULL;
    ws->nedges = 0;
}

static int cmp_cross(const void *pa, const void *pb) {
    double a = ((const RCross*)pa)->x, b = ((const RCross*)pb)->x;
    return (a > b) - (a < b);
}

int raster_fill_tiled(const RScene *s, const RBox *box, int w, int h,
                      int ss, unsigned char *buf) {
    if (!s || !box || !buf || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (s->npolys == 0) { memset(buf, 0, (size_t)w * (size_t)h); return 0; }

    int nedges = 0;
    RBox pb[32];
    for (int k = 0; k < s->npolys; ++k) {
        nedges += s->polys[k].n;
        RBox *b = &pb[k];
        b->minx = b->maxx = s->polys[k].pts[0].x;
        b->miny = b->maxy = s->polys[k].pts[0].y;
        for (int i = 1; i < s->polys[k].n; ++i) {
            double x = s->polys[k].pts[i].x, y = s->polys[k].pts[i].y;
            if (x < b->minx) b->minx = x;
            if (x > b->maxx) b->maxx = x;
            if (y < b->miny) b->miny = y;
            if (y > b->maxy) b->maxy = y;
        }
    }

    RTileWS ws;
    int rc = tile_ws_init(&ws, nedges, RASTER_TILE);
    if (rc != 0) return rc;  /* -5：初始化失败，内部已清理 */

    memset(buf, 0, (size_t)w * (size_t)h);
    double dx = (box->maxx - box->minx) / w;
    double dy = (box->maxy - box->miny) / h;

    for (int y0 = 0; y0 < h; y0 += ws.tile) {
        int y1 = y0 + ws.tile; if (y1 > h) y1 = h;
        for (int x0 = 0; x0 < w; x0 += ws.tile) {
            int x1 = x0 + ws.tile; if (x1 > w) x1 = w;

            /* 瓦片世界坐标（像素角点，比采样点更外侧，剔除保持保守）。 */
            double tMinX = box->minx + x0 * dx;
            double tMinY = box->miny + y0 * dy;
            double tMaxY = box->miny + y1 * dy;

            int ne = 0;
            for (int k = 0; k < s->npolys; ++k) {
                /* 安全剔除：
                 *  - 整体在瓦片左侧：向右射线不可能命中；
                 *  - y 范围与瓦片不相交：不存在跨跃边。
                 * 右侧多边形必须保留（射线穿入穿出贡献偶数跨）。 */
                if (pb[k].maxx <= tMinX) continue;
                if (pb[k].maxy <= tMinY || pb[k].miny >= tMaxY) continue;
                const RPoly *p = &s->polys[k];
                for (int i = 0; i < p->n; ++i) {
                    ws.edges[ne].a = p->pts[i];
                    ws.edges[ne].b = p->pts[(i + 1) % p->n];
                    ws.edges[ne].owner = k;
                    ++ne;
                }
            }
            if (ne == 0) continue;  /* 空瓦片：buf 已清零，跳过 */

            for (int y = y0; y < y1; ++y) {
                for (int j = 0; j < ss; ++j) {
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    int nc = 0;
                    for (int e = 0; e < ne; ++e) {
                        RPoint a = ws.edges[e].a, b = ws.edges[e].b;
                        if ((a.y > py) == (b.y > py)) continue;
                        double t = (py - a.y) / (b.y - a.y);
                        double xint = a.x + (b.x - a.x) * t;
                        ws.cross[nc].x = xint;
                        ws.cross[nc].owner = ws.edges[e].owner;
                        ++nc;
                    }
                    if (nc == 0) continue;
                    qsort(ws.cross, (size_t)nc, sizeof(RCross), cmp_cross);

                    /* 边界跟踪：沿 +x 走过排序交点，逐个翻转所属多边形奇偶；
                     * 闭合多边形交点总数为偶，故 count(x>px) 与 count(x<=px)
                     * 同奇偶，与原 inside() 的严格 > 判定逐位等价。 */
                    memset(ws.parity, 0, (size_t)s->npolys * sizeof(int));
                    int cur = 0, inside_n = 0;
                    for (int x = x0; x < x1; ++x) {
                        int hit = 0;
                        for (int i = 0; i < ss; ++i) {
                            double px = box->minx + (x + (i + 0.5) / ss) * dx;
                            while (cur < nc && !(ws.cross[cur].x > px)) {
                                int ow = ws.cross[cur].owner;
                                inside_n += ws.parity[ow] ? -1 : 1;
                                ws.parity[ow] ^= 1;
                                ++cur;
                            }
                            if (inside_n) { hit = 1; break; }
                        }
                        if (hit) buf[y * w + x] = 255;
                    }
                }
            }
        }
    }

    tile_ws_free(&ws);
    return 0;
}
