#include "snapshot.h"
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef RSNAP_DISABLE
/* 快照功能关闭：不实现任何快照逻辑，调用直接失败；原渲染流程不经过本文件。 */
int snapshot_save(const char *path, const RScene *s, RPlace place,
                  const RBox *win, int w, int h, long *err_off) {
    (void)path; (void)s; (void)place; (void)win; (void)w; (void)h;
    if (err_off) *err_off = -1;
    return SNAP_EDISABLED;
}
int snapshot_load(const char *path, RScene *s, RPlace *place,
                  RBox *win, int *w, int *h, long *err_off) {
    (void)path; (void)s; (void)place; (void)win; (void)w; (void)h;
    if (err_off) *err_off = -1;
    return SNAP_EDISABLED;
}
long snapshot_size_bytes(const RScene *s) { (void)s; return -1; }
const char *snapshot_strerror(int code) {
    return code == SNAP_EDISABLED ? "snapshot feature disabled at build time"
                                  : "snapshot error";
}
#else

static void put_u32(unsigned char *p, uint32_t v) {
    p[0] = (unsigned char)(v);
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}
static void put_i32(unsigned char *p, int32_t v) { put_u32(p, (uint32_t)v); }
static void put_f64(unsigned char *p, double v) {
    uint64_t u;
    memcpy(&u, &v, 8);
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)(u >> (8 * i));
}
static uint32_t get_u32(const unsigned char *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static int32_t get_i32(const unsigned char *p) { return (int32_t)get_u32(p); }
static double get_f64(const unsigned char *p) {
    uint64_t u = 0;
    for (int i = 0; i < 8; ++i) u |= (uint64_t)p[i] << (8 * i);
    double v;
    memcpy(&v, &u, 8);
    return v;
}
static int finite_f64(double v) { return isfinite(v); }

long snapshot_size_bytes(const RScene *s) {
    if (!s || s->npolys <= 0 || s->npolys > 32) return -1;
    long n = RSNAP_HEADER_LEN;
    for (int k = 0; k < s->npolys; ++k) {
        if (s->polys[k].n < 3 || s->polys[k].n > 64) return -1;
        n += 4 + 8L * 2 * s->polys[k].n;
    }
    return n;
}

static int valid_inputs(const RScene *s, RPlace place, const RBox *win,
                        int w, int h) {
    if (!s || !win || s->npolys <= 0 || s->npolys > 32) return 0;
    for (int k = 0; k < s->npolys; ++k) {
        if (s->polys[k].n < 3 || s->polys[k].n > 64) return 0;
        for (int i = 0; i < s->polys[k].n; ++i)
            if (!finite_f64(s->polys[k].pts[i].x) ||
                !finite_f64(s->polys[k].pts[i].y))
                return 0;
    }
    if (!finite_f64(place.tx) || !finite_f64(place.ty) ||
        !finite_f64(place.rot) || !finite_f64(place.scale) ||
        place.scale == 0.0)
        return 0;
    if (!finite_f64(win->minx) || !finite_f64(win->miny) ||
        !finite_f64(win->maxx) || !finite_f64(win->maxy) ||
        !(win->maxx > win->minx) || !(win->maxy > win->miny))
        return 0;
    return w > 0 && h > 0;
}

int snapshot_save(const char *path, const RScene *s, RPlace place,
                  const RBox *win, int w, int h, long *err_off) {
    if (err_off) *err_off = -1;
    if (!path || !valid_inputs(s, place, win, w, h)) return SNAP_EINVAL;
    long total = snapshot_size_bytes(s);
    if (total < 0) return SNAP_EINVAL;

    unsigned char *buf = (unsigned char*)malloc((size_t)total);
    if (!buf) return SNAP_EIO;
    memset(buf, 0, RSNAP_HEADER_LEN);
    memcpy(buf + RSNAP_OFF_MAGIC, RSNAP_MAGIC, 4);
    put_u32(buf + RSNAP_OFF_VERSION, RSNAP_VERSION);
    put_i32(buf + RSNAP_OFF_W, w);
    put_i32(buf + RSNAP_OFF_H, h);
    put_f64(buf + RSNAP_OFF_PLACE + 0, place.tx);
    put_f64(buf + RSNAP_OFF_PLACE + 8, place.ty);
    put_f64(buf + RSNAP_OFF_PLACE + 16, place.rot);
    put_f64(buf + RSNAP_OFF_PLACE + 24, place.scale);
    put_f64(buf + RSNAP_OFF_WIN + 0, win->minx);
    put_f64(buf + RSNAP_OFF_WIN + 8, win->miny);
    put_f64(buf + RSNAP_OFF_WIN + 16, win->maxx);
    put_f64(buf + RSNAP_OFF_WIN + 24, win->maxy);
    put_i32(buf + RSNAP_OFF_NPOLYS, s->npolys);

    long off = RSNAP_OFF_RECORDS;
    for (int k = 0; k < s->npolys; ++k) {
        const RPoly *p = &s->polys[k];
        put_i32(buf + off, p->n);
        off += 4;
        for (int i = 0; i < p->n; ++i) {
            put_f64(buf + off, p->pts[i].x);
            put_f64(buf + off + 8, p->pts[i].y);
            off += 16;
        }
    }

    FILE *f = fopen(path, "wb");
    if (!f) { free(buf); return SNAP_EIO; }
    size_t wr = fwrite(buf, 1, (size_t)total, f);
    int rc = fclose(f);
    free(buf);
    if (wr != (size_t)total || rc != 0) return SNAP_EIO;
    return SNAP_OK;
}

/* 读整个文件到内存，顺带得到长度，保证后续所有偏移检查都可定位。 */
static unsigned char *slurp(const char *path, long *len_out, int *rc_out) {
    FILE *f = fopen(path, "rb");
    if (!f) { *rc_out = SNAP_EIO; return NULL; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); *rc_out = SNAP_EIO; return NULL; }
    long len = ftell(f);
    if (len < 0) { fclose(f); *rc_out = SNAP_EIO; return NULL; }
    rewind(f);
    unsigned char *buf = (unsigned char*)malloc((size_t)len + 1);
    if (!buf) { fclose(f); *rc_out = SNAP_EIO; return NULL; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); *rc_out = SNAP_EIO; return NULL; }
    *len_out = len;
    *rc_out = SNAP_OK;
    return buf;
}

int snapshot_load(const char *path, RScene *s, RPlace *place,
                  RBox *win, int *w, int *h, long *err_off) {
    if (err_off) *err_off = -1;
    if (!path || !s || !place || !win || !w || !h) return SNAP_EINVAL;

    int rc;
    long len;
    unsigned char *buf = slurp(path, &len, &rc);
    if (!buf) return rc;

    /* 需要至少定长头才能读版本与多边形数。 */
    if (len < RSNAP_HEADER_LEN) {
        free(buf);
        if (err_off) *err_off = len;
        return SNAP_ETRUNC;
    }
    if (memcmp(buf + RSNAP_OFF_MAGIC, RSNAP_MAGIC, 4) != 0) {
        free(buf);
        if (err_off) *err_off = RSNAP_OFF_MAGIC;
        return SNAP_EMAGIC;
    }
    uint32_t ver = get_u32(buf + RSNAP_OFF_VERSION);
    if (ver != RSNAP_VERSION) {
        free(buf);
        if (err_off) *err_off = RSNAP_OFF_VERSION;
        return SNAP_EVERSION;
    }

    int iw = get_i32(buf + RSNAP_OFF_W);
    int ih = get_i32(buf + RSNAP_OFF_H);
    RPlace pl;
    pl.tx = get_f64(buf + RSNAP_OFF_PLACE + 0);
    pl.ty = get_f64(buf + RSNAP_OFF_PLACE + 8);
    pl.rot = get_f64(buf + RSNAP_OFF_PLACE + 16);
    pl.scale = get_f64(buf + RSNAP_OFF_PLACE + 24);
    RBox bw;
    bw.minx = get_f64(buf + RSNAP_OFF_WIN + 0);
    bw.miny = get_f64(buf + RSNAP_OFF_WIN + 8);
    bw.maxx = get_f64(buf + RSNAP_OFF_WIN + 16);
    bw.maxy = get_f64(buf + RSNAP_OFF_WIN + 24);
    int32_t npolys = get_i32(buf + RSNAP_OFF_NPOLYS);

    if (iw <= 0 || ih <= 0) {
        free(buf);
        if (err_off) *err_off = RSNAP_OFF_W;
        return SNAP_EINVAL;
    }
    if (!finite_f64(pl.tx) || !finite_f64(pl.ty) ||
        !finite_f64(pl.rot) || !finite_f64(pl.scale) || pl.scale == 0.0) {
        free(buf);
        if (err_off) *err_off = RSNAP_OFF_PLACE;
        return SNAP_EINVAL;
    }
    if (!finite_f64(bw.minx) || !finite_f64(bw.miny) ||
        !finite_f64(bw.maxx) || !finite_f64(bw.maxy) ||
        !(bw.maxx > bw.minx) || !(bw.maxy > bw.miny)) {
        free(buf);
        if (err_off) *err_off = RSNAP_OFF_WIN;
        return SNAP_EINVAL;
    }
    if (npolys <= 0 || npolys > 32) {
        free(buf);
        if (err_off) *err_off = RSNAP_OFF_NPOLYS;
        return SNAP_EINVAL;
    }

    RScene tmp;
    scene_init(&tmp);
    long off = RSNAP_OFF_RECORDS;
    for (int k = 0; k < npolys; ++k) {
        if (off + 4 > len) {
            free(buf);
            if (err_off) *err_off = off;
            return SNAP_ETRUNC;
        }
        int32_t n = get_i32(buf + off);
        if (n < 3 || n > 64) {
            free(buf);
            if (err_off) *err_off = off;
            return SNAP_EINVAL;
        }
        long rec = 4 + 16L * n;
        if (off + rec > len) {
            free(buf);
            if (err_off) *err_off = off;
            return SNAP_ETRUNC;
        }
        RPoint pts[64];
        for (int i = 0; i < n; ++i) {
            pts[i].x = get_f64(buf + off + 4 + 16 * i);
            pts[i].y = get_f64(buf + off + 4 + 16 * i + 8);
            if (!finite_f64(pts[i].x) || !finite_f64(pts[i].y)) {
                free(buf);
                if (err_off) *err_off = off + 4 + 16 * i;
                return SNAP_EINVAL;
            }
        }
        if (scene_add_poly(&tmp, pts, n) != 0) {
            free(buf);
            if (err_off) *err_off = off;
            return SNAP_EINVAL;
        }
        off += rec;
    }
    if (off != len) {
        free(buf);
        if (err_off) *err_off = off;
        return SNAP_ETRAIL;
    }
    free(buf);

    *s = tmp;
    *place = pl;
    *win = bw;
    *w = iw;
    *h = ih;
    return SNAP_OK;
}

const char *snapshot_strerror(int code) {
    switch (code) {
    case SNAP_OK: return "ok";
    case SNAP_EIO: return "io error";
    case SNAP_ETRUNC: return "snapshot truncated";
    case SNAP_EVERSION: return "unsupported snapshot version";
    case SNAP_EMAGIC: return "bad snapshot magic";
    case SNAP_EINVAL: return "invalid snapshot field";
    case SNAP_ETRAIL: return "trailing bytes after snapshot";
    case SNAP_EDISABLED: return "snapshot feature disabled at build time";
    default: return "unknown snapshot error";
    }
}
#endif
