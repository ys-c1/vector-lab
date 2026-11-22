#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/zoom.h"
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

    /* 缩放层：整数档位与权重。 */
    RZoomLevel zl;
    CK(zoom_level_from_scale(1.7, &zl) == 0);
    CK(zl.lo == 0 && zl.hi == 1 && !zl.exact);
    CK(fabs(zl.w - (log2(1.7) - 0.0)) < 1e-12);
    CK(zoom_level_from_scale(2.0, &zl) == 0 && zl.exact && zl.w == 0.0 && zl.lo == 1);
    CK(zoom_level_from_scale(0.0, &zl) == -2);
    CK(zoom_level_from_scale(-1.0, &zl) == -2);

    /* 关掉过渡时，整数层的输出必须与旧路径逐字节相同。 */
    RZoomOpts off = {-1, 4, 0};
    unsigned char a[16 * 16], c[16 * 16];
    CK(pipeline_render(&s, p, b, 16, 16, a) == 0);
    CK(pipeline_render_zoom(&s, p, b, 16, 16, &off, c, NULL) == 0);
    CK(memcmp(a, c, sizeof a) == 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
