#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/adaptive.h"
#include "src/export.h"
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

    /* ---- 方差驱动自适应采样 ---- */
    /* 标准正态分位数：z(0.975)≈1.959964，z(0.5)=0 */
    CK(fabs(norm_inv(0.975) - 1.959964) < 1e-4);
    CK(fabs(norm_inv(0.5)) < 1e-12);

    RAdaptCfg cfg;
    radapt_cfg_default(&cfg);
    RAdaptReport rep;

    /* 1. 退化情形：窗口恰为包围盒，像素全部确定性覆盖，方差=0 */
    unsigned char *ab = (unsigned char*)malloc(24 * 24);
    CK(ab != NULL);
    CK(raster_adaptive(&s, &b, 16, 16, &cfg, ab, &rep) == RADAPT_OK);
    CK(rep.degenerate == 1);
    CK(rep.var_hat == 0.0);
    CK(rep.width_obs == 0.0);
    CK(rep.samples == cfg.pilot_n);
    CK(rep.edge_pixels == 0);
    for (int i = 0; i < 16 * 16; ++i) CK(ab[i] == 255);

    /* 2. 一般情形：外扩窗口产生边界像素，宽度必须达标且 n 不超上限 */
    RBox wide = {b.minx - 6, b.miny - 6, b.maxx + 6, b.maxy + 6};
    CK(raster_adaptive(&s, &wide, 24, 24, &cfg, ab, &rep) == RADAPT_OK);
    CK(rep.degenerate == 0);
    CK(rep.rejected == 0);
    CK(rep.var_hat > 0.0);
    CK(rep.edge_pixels > 0);
    CK(rep.samples >= cfg.pilot_n && rep.samples <= cfg.nmax);
    CK(rep.width_obs <= cfg.width + 1e-12);
    CK(rep.width_pred <= cfg.width + 1e-12);
    int shades = 0;
    for (int i = 0; i < 24 * 24; ++i) if (ab[i] != 0 && ab[i] != 255) ++shades;
    CK(shades > 0); /* 灰度抗锯齿确实生效 */

    /* 3. 拒绝路径：极窄目标宽度 + 小上限 => 拒绝且不产生输出 */
    RAdaptCfg tight = cfg;
    tight.width = 0.005;
    tight.nmax = 64;
    unsigned char guard[24 * 24];
    memset(guard, 0x7e, sizeof(guard));
    memcpy(ab, guard, sizeof(guard));    CK(raster_adaptive(&s, &wide, 24, 24, &tight, ab, &rep) == RADAPT_E_REJECT);
    CK(rep.rejected == 1);
    CK(rep.width_pred > tight.width); /* 以上限也达不到目标 */
    CK(memcmp(ab, guard, sizeof(guard)) == 0); /* 拒绝时缓冲区不被写入 */

    /* 4. 参数校验 */
    RAdaptCfg badcfg = cfg;
    badcfg.pilot_n = 1;
    CK(raster_adaptive(&s, &wide, 8, 8, &badcfg, ab, &rep) == RADAPT_E_ARG);
    badcfg = cfg; badcfg.conf = 1.0;
    CK(raster_adaptive(&s, &wide, 8, 8, &badcfg, ab, &rep) == RADAPT_E_ARG);
    badcfg = cfg; badcfg.nmax = cfg.pilot_n - 1;
    CK(raster_adaptive(&s, &wide, 8, 8, &badcfg, ab, &rep) == RADAPT_E_ARG);

    /* 5. 可复现：同种子两次渲染逐字节一致 */
    unsigned char *ab2 = (unsigned char*)malloc(24 * 24);
    CK(ab2 != NULL);
    RAdaptReport rep2;
    CK(raster_adaptive(&s, &wide, 24, 24, &cfg, ab, &rep) == RADAPT_OK);
    CK(raster_adaptive(&s, &wide, 24, 24, &cfg, ab2, &rep2) == RADAPT_OK);
    CK(memcmp(ab, ab2, 24 * 24) == 0);
    CK(rep.samples == rep2.samples && rep.var_hat == rep2.var_hat);
    free(ab);
    free(ab2);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
