#include "serio.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static int g_explicit = 1;

void serio_set_mode(int explicit_bytes) { g_explicit = explicit_bytes ? 1 : 0; }
int  serio_get_mode(void) { return g_explicit; }

void serio_init(void) {
    const char *e = getenv("RENDER_SERIO");
    if (e && (!strcmp(e, "raw") || !strcmp(e, "0"))) g_explicit = 0;
    else g_explicit = 1;
}

int serio_field_count(const RScene *s) {
    if (!s) return -1;
    int n = 3; /* magic, version, npolys */
    for (int k = 0; k < s->npolys; ++k)
        n += 1 + 2 * s->polys[k].n; /* poly[k].n + 每个点 x,y */
    return n;
}

const char *serio_strerror(int code) {
    switch (code) {
        case SERIO_OK:         return "ok";
        case SERIO_E_ARG:      return "bad argument";
        case SERIO_E_IO:       return "file io error";
        case SERIO_E_MAGIC:    return "bad magic";
        case SERIO_E_VERSION:  return "unsupported version";
        case SERIO_E_LENGTH:   return "length mismatch";
        case SERIO_E_MISSING:  return "missing field";
        default:               return "unknown error";
    }
}

static void fail(SerioErr *e, int code, const char *field, const char *why) {
    if (!e) return;
    e->code = code;
    snprintf(e->field, sizeof(e->field), "%s", field ? field : "");
    snprintf(e->reason, sizeof(e->reason), "%s", why ? why : "");
}

/* ---------- 按字节编解码原语：little-endian / big-endian ---------- */

static void put_u16le(unsigned char *p, uint16_t v) {
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}
static void put_u32le(unsigned char *p, uint32_t v) {
    for (int i = 0; i < 4; ++i) p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}
static void put_u64le(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)((v >> (8 * i)) & 0xff);
}
static void put_u64be(unsigned char *p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (unsigned char)((v >> (56 - 8 * i)) & 0xff);
}

static uint16_t get_u16(const unsigned char *p, int be) {
    uint16_t v = 0;
    if (be) for (int i = 0; i < 2; ++i) v = (uint16_t)((v << 8) | p[i]);
    else    for (int i = 0; i < 2; ++i) v = (uint16_t)(v | ((uint16_t)p[i] << (8 * i)));
    return v;
}
static uint32_t get_u32(const unsigned char *p, int be) {
    uint32_t v = 0;
    if (be) for (int i = 0; i < 4; ++i) v = (v << 8) | p[i];
    else    for (int i = 0; i < 4; ++i) v |= (uint32_t)p[i] << (8 * i);
    return v;
}
static uint64_t get_u64(const unsigned char *p, int be) {
    uint64_t v = 0;
    if (be) for (int i = 0; i < 8; ++i) v = (v << 8) | p[i];
    else    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static void dbl_bytes(double d, unsigned char out[8], int be) {
    uint64_t u;
    memcpy(&u, &d, 8); /* IEEE-754 位模式，不参与任何算术 */
    if (be) put_u64be(out, u); else put_u64le(out, u);
}
static double bytes_dbl(const unsigned char in[8], int be) {
    uint64_t u = get_u64(in, be);
    double d;
    memcpy(&d, &u, 8);
    return d;
}

static int fwrite_all(const void *p, size_t n, FILE *f) {
    return fwrite(p, 1, n, f) == n ? 0 : -1;
}

/* ---------- 新路径：显式按字节（写出恒为小端） ---------- */

static int save_explicit(const RScene *s, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return SERIO_E_IO;
    int rc = SERIO_OK;
    unsigned char u32[4], db[8];
    if (fwrite_all("RSLE", 4, f)) { rc = SERIO_E_IO; goto out; }
    put_u16le(u32, (uint16_t)SERIO_VERSION);
    if (fwrite_all(u32, 2, f)) { rc = SERIO_E_IO; goto out; }
    put_u32le(u32, (uint32_t)s->npolys);
    if (fwrite_all(u32, 4, f)) { rc = SERIO_E_IO; goto out; }
    for (int k = 0; k < s->npolys; ++k) {
        const RPoly *p = &s->polys[k];
        put_u32le(u32, (uint32_t)p->n);
        if (fwrite_all(u32, 4, f)) { rc = SERIO_E_IO; goto out; }
        for (int i = 0; i < p->n; ++i) {
            dbl_bytes(p->pts[i].x, db, 0);
            if (fwrite_all(db, 8, f)) { rc = SERIO_E_IO; goto out; }
            dbl_bytes(p->pts[i].y, db, 0);
            if (fwrite_all(db, 8, f)) { rc = SERIO_E_IO; goto out; }
        }
    }
out:
    if (fclose(f) != 0 && rc == SERIO_OK) rc = SERIO_E_IO;
    return rc;
}

/* 把整个文件读进内存：*buf 由调用方释放，*len 为字节数 */
static int slurp(const char *path, unsigned char **buf, size_t *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return SERIO_E_IO;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return SERIO_E_IO; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return SERIO_E_IO; }
    rewind(f);
    unsigned char *b = (unsigned char*)malloc((size_t)sz + 1);
    if (!b) { fclose(f); return SERIO_E_IO; }
    size_t got = fread(b, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(b); return SERIO_E_IO; }
    *buf = b;
    *len = (size_t)sz;
    return SERIO_OK;
}

static int load_explicit(const char *path, RScene *out, SerioErr *err) {
    unsigned char *b = NULL;
    size_t len = 0;
    int rc = slurp(path, &b, &len);
    if (rc != SERIO_OK) { fail(err, rc, "file", "cannot read whole file"); return rc; }

    size_t pos = 0;
    /* 游标读取：need 字节，不足即漏字段 */
    #define NEED(need, fieldname) do { \
        if (pos + (need) > len) { \
            fail(err, SERIO_E_MISSING, (fieldname), "file ends before field complete"); \
            free(b); return SERIO_E_MISSING; } \
    } while (0)

    NEED(4, "magic");
    int be;
    if (!memcmp(b, "RSLE", 4)) be = 0;
    else if (!memcmp(b, "RSBE", 4)) be = 1;
    else { fail(err, SERIO_E_MAGIC, "magic", "expected RSLE/RSBE"); free(b); return SERIO_E_MAGIC; }
    pos = 4;

    NEED(2, "version");
    int ver = get_u16(b + pos, be);
    pos += 2;
    if (ver != SERIO_VERSION) {
        fail(err, SERIO_E_VERSION, "version", "only version 1 is supported");
        free(b); return SERIO_E_VERSION;
    }

    NEED(4, "npolys");
    uint32_t npolys = get_u32(b + pos, be);
    pos += 4;
    if (npolys > 32u) {
        fail(err, SERIO_E_LENGTH, "npolys", "npolys exceeds 32");
        free(b); return SERIO_E_LENGTH;
    }

    RScene tmp;
    memset(&tmp, 0, sizeof tmp);
    tmp.npolys = (int)npolys;
    for (uint32_t k = 0; k < npolys; ++k) {
        char fname[48];
        snprintf(fname, sizeof fname, "poly[%u].n", k);
        NEED(4, fname);
        uint32_t n = get_u32(b + pos, be);
        pos += 4;
        if (n < 3u || n > 64u) {
            snprintf(fname, sizeof fname, "poly[%u].n", k);
            fail(err, SERIO_E_LENGTH, fname, "vertex count must be 3..64");
            free(b); return SERIO_E_LENGTH;
        }
        tmp.polys[k].n = (int)n;
        for (uint32_t i = 0; i < n; ++i) {
            snprintf(fname, sizeof fname, "poly[%u].pts[%u].x", k, i);
            NEED(8, fname);
            tmp.polys[k].pts[i].x = bytes_dbl(b + pos, be);
            pos += 8;
            snprintf(fname, sizeof fname, "poly[%u].pts[%u].y", k, i);
            NEED(8, fname);
            tmp.polys[k].pts[i].y = bytes_dbl(b + pos, be);
            pos += 8;
        }
    }
    if (pos != len) {
        fail(err, SERIO_E_LENGTH, "payload",
             "trailing bytes: payload length does not match declared fields");
        free(b); return SERIO_E_LENGTH;
    }
    #undef NEED
    free(b);
    *out = tmp;
    fail(err, SERIO_OK, "", "");
    return SERIO_OK;
}

/* ---------- 老路径：内存直拷（同机往返，保留原行为） ---------- */

static int save_raw(const RScene *s, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return SERIO_E_IO;
    int ok = fwrite_all(s, sizeof(*s), f) == 0;
    if (fclose(f) != 0) ok = 0;
    return ok ? SERIO_OK : SERIO_E_IO;
}

static int load_raw(const char *path, RScene *out, SerioErr *err) {
    FILE *f = fopen(path, "rb");
    if (!f) { fail(err, SERIO_E_IO, "file", "cannot open"); return SERIO_E_IO; }
    RScene tmp;
    size_t got = fread(&tmp, 1, sizeof tmp, f);
    fclose(f);
    if (got != sizeof tmp) {
        fail(err, SERIO_E_LENGTH, "RScene",
             "raw blob length does not match sizeof(RScene)");
        return SERIO_E_LENGTH;
    }
    *out = tmp;
    fail(err, SERIO_OK, "", "");
    return SERIO_OK;
}

/* ---------- 对外入口：受模式开关调度 ---------- */

int scene_save(const RScene *s, const char *path) {
    if (!s || !path) return SERIO_E_ARG;
    if (g_explicit) return save_explicit(s, path);
    return save_raw(s, path);
}

int scene_load_ex(const char *path, RScene *out, SerioErr *err) {
    if (err) { err->code = SERIO_OK; err->field[0] = 0; err->reason[0] = 0; }
    if (!path || !out) { fail(err, SERIO_E_ARG, "argument", "null path/out"); return SERIO_E_ARG; }
    if (g_explicit) return load_explicit(path, out, err);
    return load_raw(path, out, err);
}

int scene_load(const char *path, RScene *out) {
    return scene_load_ex(path, out, NULL);
}
