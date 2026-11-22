#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/paramsolve.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void poly_scene(RScene *s, const double *xy, int n) {
    RPoint p[64];
    for (int i = 0; i < n; ++i) { p[i].x = xy[2 * i]; p[i].y = xy[2 * i + 1]; }
    scene_init(s);
    scene_add_poly(s, p, n);
}

int main(void) {
    /* ---- 量化表：bits=1 必须与 any-hit 一致；bits=8/ss=8 必须与 gold 一致 ---- */
    for (int ss = 1; ss <= 8; ++ss) {
        int den = ss * ss;
        CK(ps_quantize(0, ss, 1) == 0);
        CK(ps_quantize(1, ss, 1) == 255);
        CK(ps_quantize(den, ss, 1) == 255);
        for (int c = 0; c <= den; ++c) {
            int anyhit = c > 0 ? 255 : 0;
            CK(ps_quantize(c, ss, 1) == anyhit);
            /* gold: (unsigned char)(255 * c / den)，向零截断 */
            CK(ps_quantize(c, ss, 8) == (unsigned char)(255 * c / den));
        }
    }

    /* ---- 参数校验：越界必须拒绝，字段名写对 ---- */
    char field[PS_FIELD_MAX];
    CK(ps_validate_ss(0, field, sizeof field) == PS_E_SS && !strcmp(field, "ss"));
    CK(ps_validate_ss(9, field, sizeof field) == PS_E_SS && !strcmp(field, "ss"));
    CK(ps_validate_ss(-1, field, sizeof field) == PS_E_SS && !strcmp(field, "ss"));
    CK(ps_validate_ss(1, field, sizeof field) == PS_OK);
    CK(ps_validate_ss(8, field, sizeof field) == PS_OK);
    CK(ps_validate_bits(0, field, sizeof field) == PS_E_BITS && !strcmp(field, "bits"));
    CK(ps_validate_bits(9, field, sizeof field) == PS_E_BITS && !strcmp(field, "bits"));
    CK(ps_validate_bits(3, field, sizeof field) == PS_OK);

    int W = 64, H = 64;
    unsigned char *buf = (unsigned char *)malloc((size_t)W * H);
    unsigned char *buf2 = (unsigned char *)malloc((size_t)W * H);
    unsigned char *ref = (unsigned char *)malloc((size_t)W * H);

    /* 斜边四边形：足够不对称，能唯一锁定 (ss,bits)。 */
    double xy[] = {4, 4, 60, 14, 50, 60, 2, 52};
    RScene s;
    poly_scene(&s, xy, 4);
    RBox win;
    CK(scene_bbox(&s, &win) == 0);
    RPlace place = {0, 0, 0, 1.0};

    /* ---- 旧默认路径逐字节不变：pipeline_render 就是 (ss=4, bits=1) ---- */
    CK(pipeline_render(&s, place, win, W, H, buf) == 0);
    CK(ps_render(&s, place, win, W, H, 4, 1, buf2) == 0);
    CK(memcmp(buf, buf2, (size_t)W * H) == 0);

    /* 任意几何（含平移旋转缩放）也保持等价，因为两者共用同一 affine。 */
    RPlace p2 = {3.5, -2.25, 0.37, 1.3};
    if (pipeline_render(&s, p2, win, W, H, buf) == 0) {
        CK(ps_render(&s, p2, win, W, H, 4, 1, buf2) == 0);
        CK(memcmp(buf, buf2, (size_t)W * H) == 0);
    }

    /* ---- 反解：每个 (ss,bits) 自己渲染出的参照，必须能被自己匹配 ---- */
    PSResult out;
    for (int ss = 1; ss <= 8; ++ss) {
        for (int bits = 1; bits <= 8; ++bits) {
            CK(ps_render(&s, place, win, W, H, ss, bits, ref) == 0);
            int rc = ps_solve(&s, place, win, W, H, ref, 1, 8, 1, 8, 1, &out);
            CK(rc == PS_OK);
            if (rc == PS_OK) {
                CK(out.diff_bytes == 0);
                /* 选中的组合必须真的逐字节一致（可能是并列中的另一组） */
                CK(ps_render(&s, place, win, W, H, out.ss, out.bits, buf) == 0);
                CK(memcmp(buf, ref, (size_t)W * H) == 0);
                /* 选中组必须是 sets 里 ss 最小（并列取 bits 最小）的那组 */
                for (int i = 0; i < out.nmatch; ++i) {
                    const PSCombo *c = &out.matches[i];
                    int seld = (out.ss < c->ss) || (out.ss == c->ss && out.bits <= c->bits);
                    CK(seld);
                }
            }
        }
    }

    /* ---- 选取规则：max 与 min 得到的两组都必须逐字节一致 ---- */
    CK(ps_render(&s, place, win, W, H, 6, 5, ref) == 0);
    PSResult mn, mx;
    CK(ps_solve(&s, place, win, W, H, ref, 1, 8, 1, 8, 1, &mn) == PS_OK);
    CK(ps_solve(&s, place, win, W, H, ref, 1, 8, 1, 8, 0, &mx) == PS_OK);
    CK(ps_render(&s, place, win, W, H, mn.ss, mn.bits, buf) == 0);
    CK(memcmp(buf, ref, (size_t)W * H) == 0);
    CK(ps_render(&s, place, win, W, H, mx.ss, mx.bits, buf) == 0);
    CK(memcmp(buf, ref, (size_t)W * H) == 0);
    CK(mn.ss <= mx.ss);

    /* ---- 无匹配：改动参照一个字节后，原组合不再匹配但可能仍找到别的 ---- */
    CK(ps_render(&s, place, win, W, H, 3, 3, ref) == 0);
    ref[(size_t)(H / 2) * W + W / 2] ^= 0x5A;
    int rc = ps_solve(&s, place, win, W, H, ref, 3, 3, 3, 3, 1, &out);
    CK(rc == PS_E_NOMATCH);
    CK(out.nmatch == 0);
    CK(out.diff_bytes >= 1);

    /* ---- 越界拒绝：区间端点、倒置区间 ---- */
    CK(ps_render(&s, place, win, W, H, 0, 4, buf2) == PS_E_SS);
    CK(ps_render(&s, place, win, W, H, 9, 4, buf2) == PS_E_SS);
    CK(ps_render(&s, place, win, W, H, 4, 0, buf2) == PS_E_BITS);
    CK(ps_render(&s, place, win, W, H, 4, 9, buf2) == PS_E_BITS);
    CK(ps_render(&s, place, win, W, H, 4, 1, buf2) == PS_OK);
    CK(ps_solve(&s, place, win, W, H, ref, 0, 8, 1, 8, 1, &out) == PS_E_SS);
    CK(ps_solve(&s, place, win, W, H, ref, 5, 4, 1, 8, 1, &out) == PS_E_RANGE);
    CK(ps_solve(&s, place, win, W, H, ref, 1, 8, 9, 9, 1, &out) == PS_E_BITS);
    CK(ps_solve(&s, place, win, W, H, ref, 1, 8, 6, 2, 1, &out) == PS_E_RANGE);

    /* ---- 子区间搜索只在区间内找 ---- */
    CK(ps_render(&s, place, win, W, H, 7, 6, ref) == 0);
    CK(ps_solve(&s, place, win, W, H, ref, 7, 7, 6, 6, 1, &out) == PS_OK);
    CK(out.ss == 7 && out.bits == 6 && out.nmatch == 1);
    CK(ps_solve(&s, place, win, W, H, ref, 1, 6, 1, 8, 1, &out) == PS_E_NOMATCH);

    /* ---- 轴对齐矩形：所有组合都一致，必须报 ambiguous 且选 min ---- */
    double sq[] = {4, 4, 60, 4, 60, 60, 4, 60};
    RScene q;
    poly_scene(&q, sq, 4);
    RBox qb;
    CK(scene_bbox(&q, &qb) == 0);
    CK(ps_render(&q, place, qb, W, H, 5, 4, ref) == 0);
    CK(ps_solve(&q, place, qb, W, H, ref, 1, 8, 1, 8, 1, &out) == PS_OK);
    CK(out.ambiguous == 1);
    CK(out.nmatch == 64);
    CK(out.ss == 1 && out.bits == 1);
    CK(ps_render(&q, place, qb, W, H, 2, 7, buf) == 0);
    CK(memcmp(buf, ref, (size_t)W * H) == 0);   /* 任意组都逐字节一致 */

    /* ---- PGM 往返：导出后读回逐字节相同 ---- */
    CK(ps_render(&s, place, win, W, H, 7, 6, buf) == 0);
    CK(export_pgm("build/ps_roundtrip.pgm", buf, W, H) == 0);
    unsigned char *back = NULL; int bw = 0, bh = 0;
    CK(ps_load_pgm("build/ps_roundtrip.pgm", &back, &bw, &bh, field, sizeof field) == PS_OK);
    CK(bw == W && bh == H);
    if (back) { CK(memcmp(back, buf, (size_t)W * H) == 0); free(back); }
    CK(ps_load_pgm("build/does_not_exist.pgm", &back, &bw, &bh, field, sizeof field) == PS_E_IO);

    free(buf); free(buf2); free(ref);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
