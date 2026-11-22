#define _POSIX_C_SOURCE 200809L
#include "lscene.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lscene_init(LScene *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    dag_init(&s->dag);
}

static int point_in(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}

typedef struct { char a[LDAG_NAME], b[LDAG_NAME]; } NameEdge;

int lscene_load(const char *path, LScene *out, char *err, int errlen) {
    if (!path || !out) return -1;
    #define FAIL(msg) do { if (err && errlen > 0) snprintf(err, errlen, "%s", msg); return -1; } while (0)
    FILE *f = fopen(path, "r");
    if (!f) FAIL("cannot open scene file");

    lscene_init(out);
    NameEdge edges[LDAG_MAX * LDAG_MAX];
    int ne = 0;
    char order_names[LDAG_MAX][LDAG_NAME];
    int norder = 0;
    char line[1024];

    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        char *save = NULL;
        char *kw = strtok_r(line, " \t\r", &save);
        if (!kw || kw[0] == '#') continue;

        if (!strcmp(kw, "win")) {
            double v[4];
            int k = 0;
            for (; k < 4; ++k) {
                char *tok = strtok_r(NULL, " \t\r", &save);
                if (!tok) break;
                v[k] = atof(tok);
            }
            if (k != 4) FAIL("win needs 4 numbers");
            out->has_win = 1;
            out->win.minx = v[0]; out->win.miny = v[1];
            out->win.maxx = v[2]; out->win.maxy = v[3];
        } else if (!strcmp(kw, "layer")) {
            if (out->n >= LDAG_MAX) FAIL("too many layers");
            char *name = strtok_r(NULL, " \t\r", &save);
            char *sh = strtok_r(NULL, " \t\r", &save);
            if (!name || !sh) FAIL("layer needs name and shade");
            int shade = atoi(sh);
            if (shade < 0 || shade > 255) FAIL("shade out of 0..255");
            LLayer *L = &out->layers[out->n];
            memset(L, 0, sizeof(*L));
            size_t k = 0;
            while (k + 1 < LDAG_NAME && name[k]) { L->name[k] = name[k]; ++k; }
            L->name[k] = '\0';
            L->shade = (unsigned char)shade;
            int npts = 0;
            double xy[128];
            char *tx, *ty;
            while ((tx = strtok_r(NULL, " \t\r", &save)) &&
                   (ty = strtok_r(NULL, " \t\r", &save))) {
                if (npts >= 64) FAIL("too many polygon points");
                xy[2 * npts] = atof(tx);
                xy[2 * npts + 1] = atof(ty);
                ++npts;
            }
            if (npts < 3) FAIL("layer polygon needs >= 3 points");
            L->poly.n = npts;
            for (int i = 0; i < npts; ++i) {
                L->poly.pts[i].x = xy[2 * i];
                L->poly.pts[i].y = xy[2 * i + 1];
            }
            ++out->n;
        } else if (!strcmp(kw, "edge")) {
            char *a = strtok_r(NULL, " \t\r", &save);
            char *b = strtok_r(NULL, " \t\r", &save);
            if (!a || !b) FAIL("edge needs two layer names");
            if (ne >= (int)(sizeof(edges) / sizeof(edges[0]))) FAIL("too many edges");
            snprintf(edges[ne].a, LDAG_NAME, "%s", a);
            snprintf(edges[ne].b, LDAG_NAME, "%s", b);
            ++ne;
        } else if (!strcmp(kw, "order")) {
            norder = 0;
            char *name;
            while ((name = strtok_r(NULL, " \t\r", &save))) {
                if (norder >= LDAG_MAX) FAIL("too many order entries");
                snprintf(order_names[norder], LDAG_NAME, "%s", name);
                ++norder;
            }
        } else {
            FAIL("unknown record keyword");
        }
    }
    fclose(f);

    if (out->n == 0) FAIL("no layers");

    /* 图层下标即 DAG 节点下标 */
    for (int i = 0; i < out->n; ++i)
        if (dag_add_node(&out->dag, out->layers[i].name) < 0) FAIL("dup layer name");
    for (int i = 0; i < ne; ++i) {
        int u = dag_find(&out->dag, edges[i].a);
        int v = dag_find(&out->dag, edges[i].b);
        if (u < 0 || v < 0) FAIL("edge references unknown layer");
        if (dag_add_edge(&out->dag, u, v) != 0) FAIL("invalid edge");
    }

    /* 缺省窗口 = 全部图层多边形包围盒 */
    if (!out->has_win) {
        RBox bx;
        RScene tmp;
        scene_init(&tmp);
        tmp.npolys = out->n;
        for (int i = 0; i < out->n; ++i) tmp.polys[i] = out->layers[i].poly;
        if (scene_bbox(&tmp, &bx) != 0) FAIL("bbox failed");
        out->win = bx;
    }
    if (!(out->win.maxx > out->win.minx) || !(out->win.maxy > out->win.miny))
        FAIL("empty render window");

    /* 手排顺序校验：列全、无重复 */
    if (norder > 0) {
        if (norder != out->n) FAIL("order must list every layer once");
        int seen[LDAG_MAX] = {0};
        for (int i = 0; i < norder; ++i) {
            int id = dag_find(&out->dag, order_names[i]);
            if (id < 0) FAIL("order references unknown layer");
            if (seen[id]) FAIL("order contains duplicate layer");
            seen[id] = 1;
            out->order[i] = id;
        }
        out->order_n = norder;
    }
    return 0;
    #undef FAIL
}

int lscene_render(const LScene *s, const int *order, int order_n,
                  int w, int h, unsigned char *buf) {
    if (!s || !order || !buf || w <= 0 || h <= 0 || order_n != s->n) return -1;
    const RBox *win = &s->win;
    double dx = (win->maxx - win->minx) / w;
    double dy = (win->maxy - win->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double px = win->minx + (x + 0.5) * dx;
            double py = win->miny + (y + 0.5) * dy;
            unsigned char val = 0;
            for (int k = 0; k < order_n; ++k) {
                int id = order[k];
                if (id < 0 || id >= s->n) return -2;
                if (point_in(&s->layers[id].poly, px, py))
                    val = s->layers[id].shade;   /* 后渲染者覆盖 */
            }
            buf[y * w + x] = val;
        }
    return 0;
}

int lscene_pgm_diff(const char *a, const char *b, long *na, long *nb) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    if (!fa || !fb) { if (fa) fclose(fa); if (fb) fclose(fb); return -1; }
    char ma[3], mb[3];
    int wa, ha, va, wb, hb, vb;
    int ok_a = fscanf(fa, "%2s %d %d %d", ma, &wa, &ha, &va) == 4;
    int ok_b = fscanf(fb, "%2s %d %d %d", mb, &wb, &hb, &vb) == 4;
    if (!ok_a || !ok_b || strcmp(ma, "P5") || strcmp(mb, "P5") ||
        wa != wb || ha != hb || va != 255 || vb != 255) {
        fclose(fa); fclose(fb);
        return -1;
    }
    fgetc(fa); fgetc(fb);   /* 头部后单个空白符 */
    long total = (long)wa * ha, diff = 0;
    for (long i = 0; i < total; ++i) {
        int ca = fgetc(fa), cb = fgetc(fb);
        if (ca == EOF || cb == EOF) { fclose(fa); fclose(fb); return -1; }
        if (ca != cb) ++diff;
    }
    fclose(fa); fclose(fb);
    if (na) *na = total;
    if (nb) *nb = total;
    return (int)diff;
}
