#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/parallel.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 比串行多一点几何：多个相交多边形，方便暴露分块边界上的差异。 */
static void build_scene(RScene *s) {
    RPoint a[4] = {{2,3},{57,7},{51,58},{4,47}};
    RPoint b[3] = {{10,50},{60,30},{33,61}};
    RPoint c[5] = {{20,20},{45,12},{62,40},{38,55},{8,38}};
    scene_init(s);
    CK(scene_add_poly(s, a, 4) == 0);
    CK(scene_add_poly(s, b, 3) == 0);
    CK(scene_add_poly(s, c, 5) == 0);
}

static void test_parallel(void) {
    RScene s;
    build_scene(&s);
    RBox win = {0, 0, 64, 64};
    RPlace place = {3, -2, 0.3, 1.7};
    const int W = 64, H = 60;
    unsigned char *serial = (unsigned char*)malloc((size_t)W * H);
    unsigned char *par = (unsigned char*)malloc((size_t)W * H);
    unsigned char *again = (unsigned char*)malloc((size_t)W * H);
    CK(serial && par && again);
    if (!serial || !par || !again) { free(serial); free(par); free(again); return; }
    CK(pipeline_render(&s, place, win, W, H, serial) == 0);

    char err[256];
    /* 线程数 1..4，块大小分别取能整除与不能整除高度的值。 */
    for (int t = 1; t <= 4; ++t)
        for (int block = 1; block <= 20; block += 3) {
            RParallelCfg cfg = {t, block};
            if (H % block != 0) {
                CK(pipeline_render_parallel(&s, place, win, W, H, &cfg, par,
                                            err, sizeof err) == RP_ERR_DIVISIBLE);
                CK(err[0] != '\0');
                continue;
            }
            CK(pipeline_render_parallel(&s, place, win, W, H, &cfg, par,
                                        err, sizeof err) == RP_OK);
            CK(memcmp(serial, par, (size_t)W * H) == 0);
            /* 重复跑，确认并发结果可复现。 */
            CK(pipeline_render_parallel(&s, place, win, W, H, &cfg, again,
                                        err, sizeof err) == RP_OK);
            CK(memcmp(par, again, (size_t)W * H) == 0);
        }

    RParallelCfg c0 = {0, 4}, c5 = {5, 4}, cb = {2, 0}, cn = {2, 7};
    CK(pipeline_render_parallel(&s, place, win, W, H, &c0, par, err, sizeof err) == RP_ERR_THREADS);
    CK(strstr(err, "线程数") != NULL);
    CK(pipeline_render_parallel(&s, place, win, W, H, &c5, par, err, sizeof err) == RP_ERR_THREADS);
    CK(pipeline_render_parallel(&s, place, win, W, H, &cb, par, err, sizeof err) == RP_ERR_BLOCK);
    CK(pipeline_render_parallel(&s, place, win, W, H, &cn, par, err, sizeof err) == RP_ERR_DIVISIBLE);
    CK(strstr(err, "不能整除") != NULL);
    /* 配置非法时不写缓冲区。 */
    memset(par, 0x5A, (size_t)W * H);
    CK(pipeline_render_parallel(&s, place, win, W, H, &c5, par, err, sizeof err) != RP_OK);
    int untouched = 1;
    for (int i = 0; i < W * H; ++i) if (par[i] != 0x5A) untouched = 0;
    CK(untouched);

    /* 关闭并发（线程 1）时，与串行路径逐字节一致。 */
    RParallelCfg one = {1, 4};
    CK(pipeline_render_parallel(&s, place, win, W, H, &one, par, err, sizeof err) == RP_OK);
    CK(memcmp(serial, par, (size_t)W * H) == 0);

    free(serial);
    free(par);
    free(again);
}

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

    /* 按行拆分必须与整图填充逐字节相同，且不改写区间外的行。 */
    unsigned char whole[16 * 16], part[16 * 16];
    CK(raster_fill(&s, &b, 16, 16, 4, whole) == 0);
    memset(part, 0xA5, sizeof part);
    CK(raster_fill_rows(&s, &b, 16, 16, 4, 0, 4, part) == 0);
    CK(part[4 * 16] == 0xA5);          /* 区间外的行保持原值 */
    CK(raster_fill_rows(&s, &b, 16, 16, 4, 4, 16, part) == 0);
    CK(memcmp(whole, part, sizeof whole) == 0);
    CK(raster_fill_rows(&s, &b, 16, 16, 4, 4, 2, part) == -2);

    test_parallel();

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
