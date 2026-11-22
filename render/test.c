#include "src/scene.h"
#include "src/pipeline.h"
#include "src/margin.h"
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

    /* 反解留边：win=[0,100]^2 于 200x150，dx=0.5 dy=2/3 */
    RBox win = {0, 0, 100, 100};
    RMargin m;
    RBox bb1 = {-2.5, -2.5, 102.5, 102.5};
    CK(margin_solve(&bb1, &win, 200, 150, &m) == RMARGIN_OK);
    CK(m.left == 5 && m.right == 5);              /* 2.5 / 0.5 = 5 */
    CK(m.bottom == 4 && m.top == 4);              /* ceil(2.5 / (2/3)) = 4 */
    CK(margin_solve(&win, &win, 200, 150, &m) == RMARGIN_OK);
    CK(m.left == 0 && m.right == 0 && m.bottom == 0 && m.top == 0); /* 贴边解为 0 */
    RBox bb2 = {0, 0, 115, 100};
    CK(margin_solve(&bb2, &win, 200, 150, &m) == RMARGIN_REJECT);
    CK(m.right == 30);                            /* 15 / 0.5 = 30 > 200/10，拒绝仍回传 */
    RBox ex = margin_expand(&win, 200, 150, (RMargin){5, 5, 4, 4});
    CK(ex.minx == -2.5 && ex.maxx == 102.5);

    /* fit 重绘：空场景报空，超限拒绝，正常时包围盒不超出扩窗（被切为零） */
    RScene empty;
    scene_init(&empty);
    RPlace id = {0, 0, 0, 1.0};
    CK(pipeline_render_fit(&empty, id, win, 16, 16, buf, &m) == RMARGIN_EMPTY);
    RPlace big = {0, 0, 0, 2.0};
    CK(pipeline_render_fit(&s, big, b, 16, 16, buf, &m) == RMARGIN_REJECT);
    RPlace nudge = {-0.25, -0.25, 0, 1.05};
    CK(pipeline_render_fit(&s, nudge, b, 16, 16, buf, &m) == 0);
    CK(m.left == 1 && m.right == 1 && m.bottom == 1 && m.top == 1);
    RBox fb = {-0.25, -0.25, 10.25, 10.25};       /* 变换后包围盒 */
    RBox fex = margin_expand(&b, 16, 16, m);
    CK(fb.minx >= fex.minx && fb.maxx <= fex.maxx &&
       fb.miny >= fex.miny && fb.maxy <= fex.maxy);

    /* 旧零留边路径逐字节不变：钉住 16x16 全亮输出，且与直接栅格化一致 */
    unsigned char ref[16 * 16];
    CK(pipeline_render(&s, id, b, 16, 16, buf) == 0);
    CK(raster_fill(&s, &b, 16, 16, 4, ref) == 0);
    CK(memcmp(buf, ref, sizeof ref) == 0);
    long sum = 0;
    for (int i = 0; i < 16 * 16; ++i) sum += buf[i];
    CK(sum == 255L * 256);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
