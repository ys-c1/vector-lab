/* 瓦片后端初始化失败测试：用 --wrap 在第 N 次分配处注入失败，
 * 验证返回 -5、已分配资源全部释放（live 计数归零）、清理后仍可正常渲染。
 */
#include "src/scene.h"
#include "src/backend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long g_calls;          /* 分配调用计数 */
static long g_fail_at;        /* 第几次分配失败，0=永不 */
static long g_live;           /* 当前未释放分配数 */

void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void __real_free(void *);

void *__wrap_malloc(size_t n) {
    ++g_calls;
    if (g_fail_at && g_calls >= g_fail_at) return NULL;
    void *p = __real_malloc(n);
    if (p) ++g_live;
    return p;
}

void *__wrap_calloc(size_t a, size_t b) {
    ++g_calls;
    if (g_fail_at && g_calls >= g_fail_at) return NULL;
    void *p = __real_calloc(a, b);
    if (p) ++g_live;
    return p;
}

void __wrap_free(void *p) {
    if (p) { --g_live; __real_free(p); }
}

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    RPoint q[4] = {{0,0},{10,0},{10,10},{0,10}};
    RPoint t[3] = {{2,2},{8,1},{9,8}};
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, q, 4) == 0);
    CK(scene_add_poly(&s, t, 3) == 0);
    RBox box = {0, 0, 10, 10};
    int w = 64, h = 48;
    size_t n = (size_t)w * h;
    unsigned char *b1 = (unsigned char*)malloc(n), *b2 = (unsigned char*)malloc(n);
    CK(b1 && b2);

    /* tile_ws_init 依次 3 次分配：cross、edges、parity，逐个注入失败。 */
    for (long failpos = 1; failpos <= 3; ++failpos) {
        g_calls = 0; g_fail_at = failpos;
        long before = g_live;
        memset(b2, 0x55, n);
        int rc = raster_render(RASTER_BACKEND_TILE, &s, &box, w, h, 4, b2);
        CK(rc == -5);
        CK(g_live == before);           /* 失败路径无泄漏 */
        g_fail_at = 0;
    }

    /* 清理后后端仍可正常工作，且与扫描线逐字节一致。 */
    CK(raster_render(RASTER_BACKEND_SCANLINE, &s, &box, w, h, 4, b1) == 0);
    CK(raster_render(RASTER_BACKEND_TILE,     &s, &box, w, h, 4, b2) == 0);
    int diff = 0;
    for (size_t i = 0; i < n; ++i) if (b1[i] != b2[i]) ++diff;
    CK(diff == 0);

    free(b1); free(b2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
