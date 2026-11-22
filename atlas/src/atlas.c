#include "atlas.h"
#include <stdlib.h>

/* 归一化：让高 >= 宽（长边竖直）。返回 1 表示发生旋转。 */
static int normalize(const ATile *t, int *rw, int *rh) {
    if (t->w > t->h) { *rw = t->h; *rh = t->w; return 1; }
    *rw = t->w; *rh = t->h;
    return 0;
}

/* 新逻辑的优先级（旋转归一化后比较）：
 *  1) 高度（=最长边）降序——保持 FFDH 的高度单调，使 2 倍密度上界可证；
 *  2) 面积降序——同最长边时大图块优先；
 *  3) 长宽比降序——同面积时越细长越优先；
 *  4) 编号升序——同尺寸图块的确定性次序。
 */
static int smart_cmp(const void *a, const void *b) {
    const ATile *p = (const ATile *)a, *q = (const ATile *)b;
    int pw, ph, qw, qh;
    normalize(p, &pw, &ph);
    normalize(q, &qw, &qh);
    if (ph != qh) return ph > qh ? -1 : 1;
    long pa = (long)pw * ph, qa = (long)qw * qh;
    if (pa != qa) return pa > qa ? -1 : 1;
    /* 长宽比 = 高/宽（归一化后 >=1），用交叉相乘避免浮点 */
    long lp = (long)ph * qw, lq = (long)qh * pw;
    if (lp != lq) return lp > lq ? -1 : 1;
    if (p->id != q->id) return p->id < q->id ? -1 : 1;
    return 0;
}

ATile *atlas_sorted(const ATile *tiles, int n) {
    if (!tiles || n <= 0) return NULL;
    ATile *s = (ATile *)malloc(sizeof(ATile) * (size_t)n);
    if (!s) return NULL;
    for (int i = 0; i < n; ++i) s[i] = tiles[i];
    qsort(s, (size_t)n, sizeof(ATile), smart_cmp);
    return s;
}

void atlas_result_init(AResult *r) {
    r->items = NULL; r->count = 0; r->cap = 0;
    r->bins = 0; r->atlas_w = 0; r->atlas_h = 0;
    r->reject_id = -1;
}

void atlas_result_free(AResult *r) {
    if (!r) return;
    free(r->items);
    atlas_result_init(r);
}

static int push_place(AResult *r, APlace p) {
    if (r->count == r->cap) {
        int nc = r->cap ? r->cap * 2 : 8;
        APlace *ni = (APlace *)realloc(r->items, sizeof(APlace) * (size_t)nc);
        if (!ni) return ATLAS_E_MEM;
        r->items = ni; r->cap = nc;
    }
    r->items[r->count++] = p;
    return ATLAS_OK;
}

typedef struct {
    int y;       /* 货架顶边 y */
    int height;  /* 货架高度（首个图块的归一化高） */
    int used;    /* 已占用宽度 */
} Shelf;

typedef struct {
    Shelf *s;
    int n, cap;
    int bottom;  /* 已开货架累计高度 */
} Page;

static int page_add_shelf(Page *pg, int h) {
    if (pg->n == pg->cap) {
        int nc = pg->cap ? pg->cap * 2 : 8;
        Shelf *ns = (Shelf *)realloc(pg->s, sizeof(Shelf) * (size_t)nc);
        if (!ns) return ATLAS_E_MEM;
        pg->s = ns; pg->cap = nc;
    }
    pg->s[pg->n].y = pg->bottom;
    pg->s[pg->n].height = h;
    pg->s[pg->n].used = 0;
    ++pg->n;
    pg->bottom += h;
    return ATLAS_OK;
}

int atlas_pack(const ATile *tiles, int n, int w, int h, int order, AResult *out) {
    if (!tiles || !out || n < 1 || w <= 0 || h <= 0 ||
        (order != ATLAS_ORDER_INPUT && order != ATLAS_ORDER_SMART))
        return ATLAS_E_ARG;

    const ATile *seq = tiles;
    ATile *owned = NULL;
    if (order == ATLAS_ORDER_SMART) {
        owned = atlas_sorted(tiles, n);
        if (!owned) return ATLAS_E_MEM;
        seq = owned;
    }

    atlas_result_init(out);
    out->atlas_w = w; out->atlas_h = h;

    Page pg = {0};
    int rc = ATLAS_OK;
    for (int i = 0; i < n; ++i) {
        int rw, rh;
        int rotated = normalize(&seq[i], &rw, &rh);
        if (rw > w || rh > h) {
            rc = ATLAS_E_TOO_LARGE;
            out->reject_id = seq[i].id;
            goto done;
        }

        int fit = -1;
        for (int k = 0; k < pg.n; ++k) {
            if (rh <= pg.s[k].height && pg.s[k].used + rw <= w) { fit = k; break; }
        }
        if (fit < 0) {
            if (pg.bottom + rh > h) {
                /* 当前页放不下新货架：结算本页，开新页 */
                free(pg.s);
                pg = (Page){0};
                ++out->bins;
            }
            rc = page_add_shelf(&pg, rh);
            if (rc != ATLAS_OK) goto done;
            fit = pg.n - 1;
        }
        Shelf *sh = &pg.s[fit];
        APlace pl;
        pl.id = seq[i].id;
        pl.bin = out->bins;
        pl.x = sh->used;
        pl.y = sh->y;
        pl.w = rw; pl.h = rh;
        pl.rotated = rotated;
        rc = push_place(out, pl);
        if (rc != ATLAS_OK) goto done;
        sh->used += rw;
    }
    if (pg.n > 0) ++out->bins;

done:
    free(pg.s);
    free(owned);
    return rc;
}

double atlas_waste(const AResult *r) {
    if (!r || r->count == 0 || r->atlas_w <= 0) return -1.0;
    /* 每页紧致包围盒高度 = 该页最大货架底边（y+h）。
     * FFDH 会把后处理的图块回填到更早的货架，不能只看最后一条记录，
     * 因此逐页维护 page_bottom 最大值。 */
    long bound = 0;
    long used = 0;
    int cur_bin = r->items[0].bin;
    int page_bottom = 0;
    for (int i = 0; i < r->count; ++i) {
        const APlace *p = &r->items[i];
        if (p->bin != cur_bin) {
            bound += (long)page_bottom * r->atlas_w;
            cur_bin = p->bin;
            page_bottom = 0;
        }
        int shelf_bottom = p->y + p->h;
        if (shelf_bottom > page_bottom) page_bottom = shelf_bottom;
        used += (long)p->w * p->h;
    }
    bound += (long)page_bottom * r->atlas_w;
    if (bound <= 0) return -1.0;
    return (double)(bound - used) / (double)bound;
}
