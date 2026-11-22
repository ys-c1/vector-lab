#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/gamma.h"
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

    /* ---- gamma 模块 ---- */
    char field[GAM_FIELD_MAX];
    /* 编码端点恒等与单调性 */
    CK(gamma_encode_level(0, 2.2) == 0);
    CK(gamma_encode_level(255, 2.2) == 255);
    CK(gamma_encode_level(128, 1.0) == 128);
    int mono = 1;
    for (int v = 1; v < 256; ++v)
        if (gamma_encode_level(v, 2.2) < gamma_encode_level(v - 1, 2.2)) mono = 0;
    CK(mono);
    CK(gamma_encode_level(64, 2.2) > 64); /* gamma>1 提亮中间调 */

    /* gamma 参数校验：0 / 负 / NaN / Inf 都拒绝并指出字段 */
    field[0] = 0;
    CK(gamma_validate(0.0, field, sizeof field) == GAM_E_GAMMA);
    CK(!strcmp(field, "gamma"));
    CK(gamma_validate(-2.0, field, sizeof field) == GAM_E_GAMMA);
    CK(gamma_validate(NAN, field, sizeof field) == GAM_E_GAMMA);
    CK(gamma_validate(INFINITY, field, sizeof field) == GAM_E_GAMMA);
    CK(gamma_validate(0.5, field, sizeof field) == GAM_OK);

    unsigned char px[4] = {0, 64, 128, 255}, out[4];
    CK(gamma_apply(px, 2, 2, 0.0, out, field, sizeof field) == GAM_E_GAMMA);
    CK(!strcmp(field, "gamma"));
    CK(gamma_apply(px, 2, 2, 2.0, out, field, sizeof field) == GAM_OK);
    CK(out[0] == 0 && out[3] == 255 && out[1] == gamma_encode_level(64, 2.0));

    /* 已知 gamma 反解：残差须为 0，平台中心重编码逐字节相同 */
    enum { N = 4096 };
    static unsigned char src[N], ref[N], chk[N];
    for (int i = 0; i < N; ++i)
        src[i] = (unsigned char)(i % 256);
    for (int i = 0; i < N; ++i)
        ref[i] = gamma_encode_level(src[i], 1.7);
    GammaFit fit;
    CK(gamma_fit(src, ref, 64, 64, &fit) == GAM_OK);
    CK(fit.residual == 0.0);
    CK(fabs(fit.gamma - 1.7) / 1.7 < 0.01);
    CK(fit.lo < fit.gamma && fit.gamma < fit.hi);
    CK(gamma_apply(src, 64, 64, fit.gamma, chk, field, sizeof field) == GAM_OK);
    CK(memcmp(chk, ref, N) == 0);

    /* 关闭编码（gamma=1）时逐字节恒等 */
    CK(gamma_apply(src, 64, 64, 1.0, chk, field, sizeof field) == GAM_OK);
    CK(memcmp(chk, src, N) == 0);

    /* 全黑 / 全白无法拟合，报出灰度级与字段 */
    size_t hb[256] = {0}, hw[256] = {0}, hmix[256] = {0};
    hb[0] = 500;
    hw[255] = 500;
    hmix[10] = 250; hmix[240] = 250;
    CK(gamma_fit_hist(hb, 500, hmix, 500, &fit) == GAM_E_FLAT);
    CK(fit.flat == 1 && fit.flat_level == 0 && !strcmp(fit.err_field, "src"));
    CK(gamma_fit_hist(hw, 500, hmix, 500, &fit) == GAM_E_FLAT);
    CK(fit.flat_level == 255 && !strcmp(fit.err_field, "src"));
    CK(gamma_fit_hist(hmix, 500, hb, 500, &fit) == GAM_E_FLAT);
    CK(fit.flat_level == 0 && !strcmp(fit.err_field, "ref"));

    /* 单调步进图（每级都出现，且无量化歧义）残差应为 0 */
    for (int i = 0; i < N; ++i) src[i] = (unsigned char)(i % 256);
    for (int i = 0; i < N; ++i) ref[i] = gamma_encode_level(src[i], 0.8);
    CK(gamma_fit(src, ref, 64, 64, &fit) == GAM_OK);
    CK(fit.residual == 0.0 && fabs(fit.gamma - 0.8) / 0.8 < 0.01);

    /* 尺寸/参数错误 */
    CK(gamma_fit(NULL, ref, 64, 64, &fit) == GAM_E_ARG);
    CK(gamma_fit(src, ref, 0, 64, &fit) == GAM_E_ARG);
    CK(gamma_fit_hist(NULL, 1, hmix, 1, &fit) == GAM_E_ARG);

    /* 残差函数本身：同一输入残差为 0，不同 gamma 时非负 */
    size_t hs[256], hr[256];
    gamma_hist(src, 64, 64, hs);
    gamma_hist(ref, 64, 64, hr);
    CK(gamma_residual_hist(hs, N, hr, N, 0.8) == 0.0);
    CK(gamma_residual_hist(hs, N, hr, N, 3.0) >= 0.0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
