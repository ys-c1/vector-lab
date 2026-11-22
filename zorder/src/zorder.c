#include "zorder.h"
#include <math.h>
#include <string.h>

static int inside(const ZPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        ZPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

int zorder_poly_bbox(const ZPoly *p, ZBox *out) {
    if (!p || !out || p->n < 1) return -1;
    out->minx = out->maxx = p->pts[0].x;
    out->miny = out->maxy = p->pts[0].y;
    for (int i = 1; i < p->n; ++i) {
        if (p->pts[i].x < out->minx) out->minx = p->pts[i].x;
        if (p->pts[i].x > out->maxx) out->maxx = p->pts[i].x;
        if (p->pts[i].y < out->miny) out->miny = p->pts[i].y;
        if (p->pts[i].y > out->maxy) out->maxy = p->pts[i].y;
    }
    return 0;
}

/* 像素 (px,py) 的全部 ss*ss 个子采样点都在多边形内 */
static int pixel_covered(const ZPoly *p, ZBox win, int w, int h, int ss, int px, int py) {
    double dx = (win.maxx - win.minx) / w;
    double dy = (win.maxy - win.miny) / h;
    for (int j = 0; j < ss; ++j)
        for (int i = 0; i < ss; ++i) {
            double x = win.minx + (px + (i + 0.5) / ss) * dx;
            double y = win.miny + (py + (j + 0.5) / ss) * dy;
            if (!inside(p, x, y)) return 0;
        }
    return 1;
}

int zorder_solve(const ZPoly *polys, const unsigned char *colors, int n,
                 const unsigned char *target, ZBox win, int w, int h, int ss,
                 int *order, ZStats *stats, int *cyc, int *ncyc) {
    if (!polys || !colors || !target || !order) return -1;
    if (n < 1 || n > ZO_MAX || w <= 0 || h <= 0 || ss < 1 || ss > 8) return -1;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -1;
    if (ncyc) *ncyc = 0;
    ZStats st = {0, 0, 0, 0};
    ZBox bb[ZO_MAX];
    for (int i = 0; i < n; ++i)
        if (zorder_poly_bbox(&polys[i], &bb[i]) != 0) return -1;
    char dir[ZO_MAX][ZO_MAX]; /* dir[i][j]=1: i 先画（j 盖 i） */
    memset(dir, 0, sizeof dir);
    double dx = (win.maxx - win.minx) / w;
    double dy = (win.maxy - win.miny) / h;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) {
            double lox = bb[i].minx > bb[j].minx ? bb[i].minx : bb[j].minx;
            double hix = bb[i].maxx < bb[j].maxx ? bb[i].maxx : bb[j].maxx;
            double loy = bb[i].miny > bb[j].miny ? bb[i].miny : bb[j].miny;
            double hiy = bb[i].maxy < bb[j].maxy ? bb[i].maxy : bb[j].maxy;
            if (lox > hix || loy > hiy) continue;              /* 不相交 */
            if ((hix - lox) * (hiy - loy) == 0.0) {            /* 零面积接触 */
                st.zero_area++;
                continue;
            }
            st.pairs++;
            int px0 = (int)floor((lox - win.minx) / dx); if (px0 < 0) px0 = 0;
            int px1 = (int)ceil((hix - win.minx) / dx);  if (px1 > w) px1 = w;
            int py0 = (int)floor((loy - win.miny) / dy); if (py0 < 0) py0 = 0;
            int py1 = (int)ceil((hiy - win.miny) / dy);  if (py1 > h) py1 = h;
            int ev = 0; /* 1: i 盖 j；2: j 盖 i；3: 矛盾（双向证据） */
            for (int py = py0; py < py1; ++py)
                for (int px = px0; px < px1; ++px) {
                    if (!pixel_covered(&polys[i], win, w, h, ss, px, py)) continue;
                    if (!pixel_covered(&polys[j], win, w, h, ss, px, py)) continue;
                    unsigned char v = target[py * w + px];
                    if (v == colors[i] && v != colors[j]) ev |= 1;
                    else if (v == colors[j] && v != colors[i]) ev |= 2;
                    /* 第三形颜色或边界混合色：无证据 */
                }
            if (!ev) { st.undecided++; continue; }
            if (ev & 1) { dir[j][i] = 1; st.edges++; }
            if (ev & 2) { dir[i][j] = 1; st.edges++; }
        }
    /* Kahn 拓扑排序，平手取添加顺序靠前者：无约束时维持原顺序 */
    int indeg[ZO_MAX] = {0};
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j)
            if (dir[i][j]) indeg[j]++;
    char done[ZO_MAX] = {0};
    int emitted = 0;
    while (emitted < n) {
        int pick = -1;
        for (int k = 0; k < n; ++k)
            if (!done[k] && indeg[k] == 0) { pick = k; break; }
        if (pick < 0) break;
        done[pick] = 1;
        order[emitted++] = pick;
        for (int k = 0; k < n; ++k)
            if (dir[pick][k]) indeg[k]--;
    }
    if (stats) *stats = st;
    if (emitted == n) return 0;
    /* 环依赖：剩余节点中能沿边回到自己的即环上图形 */
    int nc = 0;
    for (int v = 0; v < n; ++v) {
        if (done[v]) continue;
        char vis[ZO_MAX] = {0};
        int stack[ZO_MAX * ZO_MAX], sp = 0;
        for (int k = 0; k < n; ++k)
            if (dir[v][k] && !done[k]) stack[sp++] = k;
        while (sp > 0) {
            int u = stack[--sp];
            if (u == v) { if (cyc) cyc[nc] = v; ++nc; break; }
            if (vis[u]) continue;
            vis[u] = 1;
            for (int k = 0; k < n; ++k)
                if (dir[u][k] && !done[k] && !vis[k]) stack[sp++] = k;
        }
    }
    if (ncyc) *ncyc = nc;
    return -2;
}

void zorder_compose(const ZPoly *polys, const unsigned char *colors, int n,
                    const int *order, ZBox win, int w, int h, int ss,
                    unsigned char *out) {
    if (!polys || !colors || !order || !out || n < 1 || w <= 0 || h <= 0 || ss < 1) return;
    double dx = (win.maxx - win.minx) / w;
    double dy = (win.maxy - win.miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sum = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double sx = win.minx + (x + (i + 0.5) / ss) * dx;
                    double sy = win.miny + (y + (j + 0.5) / ss) * dy;
                    for (int k = n - 1; k >= 0; --k)
                        if (inside(&polys[order[k]], sx, sy)) {
                            sum += colors[order[k]];
                            break;
                        }
                }
            out[y * w + x] = (unsigned char)(sum / (ss * ss));
        }
}

int zorder_render_scene(const ZPoly *polys, const unsigned char *colors, int n,
                        const unsigned char *target, ZBox win, int w, int h, int ss,
                        int enabled, unsigned char *out) {
    if (!polys || !out || n < 1 || n > ZO_MAX || w <= 0 || h <= 0 || ss < 1 || ss > 8)
        return -1;
    if (!(win.maxx > win.minx) || !(win.maxy > win.miny)) return -1;
    if (!enabled) {
        /* 旧逻辑：添加顺序、二值并集，与 render 模块 raster_fill 逐字节一致 */
        double dx = (win.maxx - win.minx) / w;
        double dy = (win.maxy - win.miny) / h;
        memset(out, 0, (size_t)w * (size_t)h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int hit = 0;
                for (int j = 0; j < ss && !hit; ++j)
                    for (int i = 0; i < ss && !hit; ++i) {
                        double sx = win.minx + (x + (i + 0.5) / ss) * dx;
                        double sy = win.miny + (y + (j + 0.5) / ss) * dy;
                        for (int k = 0; k < n; ++k)
                            if (inside(&polys[k], sx, sy)) { hit = 1; break; }
                    }
                out[y * w + x] = hit ? 255 : 0;
            }
        return 0;
    }
    if (!colors || !target) return -1;
    int order[ZO_MAX], cyc[ZO_MAX], ncyc = 0;
    ZStats st;
    int rc = zorder_solve(polys, colors, n, target, win, w, h, ss, order, &st, cyc, &ncyc);
    if (rc != 0) return rc;
    zorder_compose(polys, colors, n, order, win, w, h, ss, out);
    return 0;
}

int zorder_diff(const unsigned char *a, const unsigned char *b, int w, int h) {
    if (!a || !b || w <= 0 || h <= 0) return -1;
    int n = 0;
    for (int i = 0; i < w * h; ++i)
        if (a[i] != b[i]) ++n;
    return n;
}
