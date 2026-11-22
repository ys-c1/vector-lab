#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
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

    /* 自适应旋转：opt_rot=0 时输出必须与原零旋转路径逐字节一致 */
    unsigned char ref[16 * 16], got[16 * 16];
    CK(pipeline_render(&s, p, b, 16, 16, ref) == 0);
    double ang = -1.0;
    CK(pipeline_render_fit(&s, p, 16, 16, 0, got, &ang) == 0);
    CK(ang == 0.0);
    for (int i = 0; i < 16 * 16; ++i) CK(ref[i] == got[i]);

    /* 倾斜 30 度的长矩形：fit 模式应转回轴对齐，像素占用不增加且角度被接受 */
    RScene t;
    scene_init(&t);
    RPoint tb[4] = {{-10,-1.5},{10,-1.5},{10,1.5},{-10,1.5}};
    double c = cos(0.5235987755982989), sn = sin(0.5235987755982989);
    RPoint tq[4];
    for (int i = 0; i < 4; ++i) {
        tq[i].x = tb[i].x * c - tb[i].y * sn;
        tq[i].y = tb[i].x * sn + tb[i].y * c;
    }
    CK(scene_add_poly(&t, tq, 4) == 0);
    unsigned char b0[24 * 24], b1[24 * 24];
    CK(pipeline_render_fit(&t, p, 24, 24, 0, b0, &ang) == 0);
    int on0 = export_count_on(b0, 24, 24);
    CK(pipeline_render_fit(&t, p, 24, 24, 1, b1, &ang) == 0);
    CK(ang > 0.0 && ang < 1.5707963267948966);          /* 角度落在 [0, pi/2) */
    CK(export_count_on(b1, 24, 24) <= on0);              /* 旋转后白占像素不增加 */

    /* 入参防御 */
    CK(pipeline_render_fit(NULL, p, 16, 16, 1, buf, &ang) == -1);
    CK(pipeline_render_fit(&s, zero, 16, 16, 1, buf, &ang) == -2);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
