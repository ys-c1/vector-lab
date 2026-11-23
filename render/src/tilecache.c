#include "tilecache.h"
#include <stdlib.h>
#include <string.h>

static uint64_t mix64(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static uint64_t key_hash(int cls, int tx, int ty) {
    uint64_t h = 0x9e3779b97f4a7c15ULL;
    h = mix64(h ^ (uint64_t)(uint32_t)cls);
    h = mix64(h ^ (uint64_t)(uint32_t)tx);
    h = mix64(h ^ (uint64_t)(uint32_t)ty);
    return h;
}

static int table_grow(TileCache *tc) {
    size_t ncap = tc->cap ? tc->cap * 2 : 64;
    TCSlot *ns = (TCSlot *)calloc(ncap, sizeof(TCSlot));
    if (!ns) return TC_ERR_NOMEM;
    for (size_t i = 0; i < tc->cap; ++i) {
        if (!tc->slots[i].used) continue;
        size_t j = tc->slots[i].hash & (ncap - 1);
        while (ns[j].used) j = (j + 1) & (ncap - 1);
        ns[j] = tc->slots[i];
    }
    free(tc->slots);
    tc->slots = ns;
    tc->cap = ncap;
    return TC_OK;
}

int tc_init(TileCache *tc, int tw, int th) {
    if (!tc || tw <= 0 || th <= 0) return TC_ERR_ARG; /* 尺寸为零不许进 */
    memset(tc, 0, sizeof(*tc));
    tc->tw = tw;
    tc->th = th;
    tc->pool_cap = (size_t)tw * th * 16;
    tc->pool = (unsigned char *)malloc(tc->pool_cap);
    if (!tc->pool) return TC_ERR_NOMEM;
    return table_grow(tc);
}

void tc_free(TileCache *tc) {
    if (!tc) return;
    free(tc->slots);
    free(tc->pool);
    memset(tc, 0, sizeof(*tc));
}

/* 命中返回 1；未命中返回 0 且 *idx 指向应插入的槽；键冲突返回 TC_ERR_COLLISION。 */
static int probe(TileCache *tc, int cls, int tx, int ty, uint64_t h, size_t *idx) {
    size_t i = h & (tc->cap - 1);
    while (tc->slots[i].used) {
        if (tc->slots[i].hash == h) {
            if (tc->slots[i].cls == cls && tc->slots[i].tx == tx && tc->slots[i].ty == ty) {
                *idx = i;
                return 1;
            }
            ++tc->collisions; /* 哈希相同而键不同：算失败 */
            return TC_ERR_COLLISION;
        }
        i = (i + 1) & (tc->cap - 1);
    }
    *idx = i;
    return 0;
}

int tc_get(TileCache *tc, int cls, int tx, int ty, const unsigned char **pix) {
    if (!tc || !tc->slots || !pix) return TC_ERR_ARG;
    size_t i;
    int r = probe(tc, cls, tx, ty, key_hash(cls, tx, ty), &i);
    if (r < 0) return r;
    if (r) {
        ++tc->hits;
        *pix = tc->pool + tc->slots[i].off;
        return 1;
    }
    ++tc->misses;
    return 0;
}

int tc_put(TileCache *tc, int cls, int tx, int ty, const unsigned char *pix) {
    if (!tc || !pix) return TC_ERR_ARG;
    size_t need = (size_t)tc->tw * tc->th;
    if (tc->pool_used + need > tc->pool_cap) {
        size_t ncap = tc->pool_cap * 2;
        while (ncap < tc->pool_used + need) ncap *= 2;
        unsigned char *np = (unsigned char *)realloc(tc->pool, ncap);
        if (!np) return TC_ERR_NOMEM;
        tc->pool = np;
        tc->pool_cap = ncap;
    }
    if ((tc->len + 1) * 10 >= tc->cap * 7) {
        int r = table_grow(tc);
        if (r != TC_OK) return r;
    }
    uint64_t h = key_hash(cls, tx, ty);
    size_t i;
    int r = probe(tc, cls, tx, ty, h, &i);
    if (r < 0) return r;
    if (r) return TC_OK; /* 同键已存在，内容确定性相同，无需覆盖 */
    memcpy(tc->pool + tc->pool_used, pix, need);
    tc->slots[i].hash = h;
    tc->slots[i].cls = cls;
    tc->slots[i].tx = tx;
    tc->slots[i].ty = ty;
    tc->slots[i].off = tc->pool_used;
    tc->slots[i].used = 1;
    tc->pool_used += need;
    ++tc->len;
    return TC_OK;
}
