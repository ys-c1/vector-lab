#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
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

    /* 精简路径：合法比例出图、非法比例拒绝、退化多边形拒绝 */
    unsigned char buf2[16 * 16];
    int removed = -1;
    CK(pipeline_render_simplified(&s, p, b, 16, 16, 0.9, buf2, &removed) == 0);
    CK(removed == 0); /* 正方形无共线点、无可剔顶点 */
    CK(pipeline_render_simplified(&s, p, b, 16, 16, 1.5, buf2, &removed) == -2);
    RPoint flatline[3] = {{0,0},{1,1},{2,2}};
    RScene sf;
    scene_init(&sf);
    CK(scene_add_poly(&sf, flatline, 3) == 0);
    RBox bf = {0, 0, 2, 2};
    CK(pipeline_render_simplified(&sf, p, bf, 8, 8, 0.9, buf2, &removed) == -5);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
