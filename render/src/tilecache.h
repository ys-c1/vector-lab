#ifndef RTILECACHE_H
#define RTILECACHE_H
#include <stddef.h>
#include <stdint.h>

/* 图块缓存：同一几何类别的图块只光栅化一次。
 * 键 = {cls, tx, ty}：
 *   TC_EMPTY —— 块矩形不跨任何边且角点在所有多边形外，内容恒为 0，单例键
 *   TC_FULL  —— 块矩形不跨任何边且角点在某个多边形内，内容恒为 255，单例键
 *   TC_MIXED —— 其余块，键含块坐标，内容逐像素光栅化
 * 查找时比较完整键；64 位哈希相同而键不同 = 键冲突，记为失败。
 */
enum { TC_EMPTY = 0, TC_FULL = 1, TC_MIXED = 2 };

typedef struct {
    uint64_t hash;
    int cls, tx, ty;
    size_t off; /* 像素在 pool 中的偏移，tw*th 字节 */
    int used;
} TCSlot;

typedef struct {
    int tw, th;
    TCSlot *slots;
    size_t cap, len;
    unsigned char *pool;
    size_t pool_used, pool_cap;
    long hits, misses, collisions;
} TileCache;

enum { TC_OK = 0, TC_ERR_ARG = -1, TC_ERR_NOMEM = -2, TC_ERR_COLLISION = -3 };

/* tw/th 必须 > 0；为零或负直接拒绝（TC_ERR_ARG）。 */
int tc_init(TileCache *tc, int tw, int th);
void tc_free(TileCache *tc);
/* 命中返回 1 且 *pix 指向缓存像素；未命中返回 0；键冲突返回 TC_ERR_COLLISION。 */
int tc_get(TileCache *tc, int cls, int tx, int ty, const unsigned char **pix);
int tc_put(TileCache *tc, int cls, int tx, int ty, const unsigned char *pix);

#endif
