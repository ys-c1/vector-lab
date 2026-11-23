#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/tilecache.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    RPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    RScene s;
    scene_init(&s);
    CK(scene_add_poly(&s, sq, 4) == 0);
    CK(scene_count_verts(&s) == 4);
    RBox b;
    CK(scene_bbox(&s, &b) == 0);
    CK(b.minx == 0 && b.maxx == 10);
    unsigned char buf[16 * 16];
    CK(raster_fill(&s, &b, 16, 16, 4, buf) == 0);
    int on = export_count_on(buf, 16, 16);
    CK(on > 240 && on < 260);
    RPlace p = {0, 0, 0, 1.0};
    CK(pipeline_render(&s, p, b, 16, 16, buf) == 0);
    RPlace zero = {0, 0, 0, 0.0};
    CK(pipeline_render(&s, zero, b, 16, 16, buf) == -2);
    RBox bad = {0, 0, 0, 0};
    CK(pipeline_render(&s, p, bad, 16, 16, buf) == -3);
    CK(scene_add_poly(&s, sq, 2) == -1);

    /* 图块缓存 */
    TileCache tc;
    CK(tc_init(&tc, 0, 8) == TC_ERR_ARG);   /* 尺寸为零不许进 */
    CK(tc_init(&tc, 8, 0) == TC_ERR_ARG);
    CK(tc_init(&tc, -4, 8) == TC_ERR_ARG);
    CK(tc_init(&tc, 8, 8) == TC_OK);
    unsigned char tile_a[64], tile_b[64];
    memset(tile_a, 0x11, sizeof tile_a);
    memset(tile_b, 0x22, sizeof tile_b);
    CK(tc_put(&tc, TC_MIXED, 2, 3, tile_a) == TC_OK);
    CK(tc_put(&tc, TC_MIXED, 5, 7, tile_b) == TC_OK);
    const unsigned char *got = NULL;
    CK(tc_get(&tc, TC_MIXED, 2, 3, &got) == 1 && got[0] == 0x11);
    CK(tc_get(&tc, TC_MIXED, 5, 7, &got) == 1 && got[0] == 0x22);
    CK(tc_get(&tc, TC_MIXED, 9, 9, &got) == 0); /* 未命中不串键 */
    CK(tc.collisions == 0);
    tc_free(&tc);

    /* 缓存开/关逐字节一致（含非整除边界的 100x76，块 16） */
    RPoint star[10];
    for (int i = 0; i < 10; ++i) {
        double a = 0.62831853 * i;
        double r = (i & 1) ? 3.0 : 8.0;
        star[i].x = 5.0 + r * cos(a);
        star[i].y = 5.0 + r * sin(a);
    }
    RScene s2;
    scene_init(&s2);
    CK(scene_add_poly(&s2, sq, 4) == 0);
    CK(scene_add_poly(&s2, star, 10) == 0);
    RBox b2;
    CK(scene_bbox(&s2, &b2) == 0);
    int W = 100, H = 76;
    unsigned char *ref = malloc((size_t)W * H), *out = malloc((size_t)W * H);
    CK(ref && out);
    RPlace one = {0, 0, 0, 1.0};
    CK(pipeline_render(&s2, one, b2, W, H, ref) == 0);
    TileCache tc2;
    CK(tc_init(&tc2, 16, 16) == TC_OK);
    CK(pipeline_render_tc(&s2, one, b2, W, H, out, &tc2) == 0);
    CK(tc2.collisions == 0);
    CK(memcmp(ref, out, (size_t)W * H) == 0);
    CK(tc2.hits > 0); /* 同一场景内确有复用 */
    tc_free(&tc2);
    free(ref);
    free(out);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
