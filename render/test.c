#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
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
    /* 新逻辑：mode 0 必须与旧管线逐字节一致；mode 1 覆盖率面积正确 */
    unsigned char b0[16 * 16], b1[16 * 16], b2[16 * 16];
    RBox wide = {-1, -1, 11, 11};
    CK(pipeline_render(&s, p, wide, 16, 16, b0) == 0);
    CK(pipeline_render2(&s, p, wide, 16, 16, 0, 0.5, 0.5, b1) == 0);
    CK(memcmp(b0, b1, sizeof b0) == 0);
    CK(pipeline_render2(&s, p, wide, 16, 16, 1, 0.5, 0.5, b2) == 0);
    long sum = 0;
    for (int i = 0; i < 16 * 16; ++i) sum += b2[i];
    CK(sum > 170 * 255 && sum < 186 * 255); /* 100/144 面积占比 */
    CK(pipeline_render2(&s, zero, wide, 16, 16, 1, 0.5, 0.5, b2) == -2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
