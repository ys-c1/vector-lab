#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int nonzero_count(const unsigned char *b, int n) {
    int c = 0;
    for (int i = 0; i < n; ++i) if (b[i] > 0) ++c;
    return c;
}

/* 独立参照：与改动前 raster_fill 同样的等分 4x4、二值命中，单独写一份，
 * 用于证明关闭掩码新逻辑时，主路径产出逐字节不变。 */
static int ref_inside(const RPoly *p, double x, double y) {
    int c = 0;
    for (int i = 0; i < p->n; ++i) {
        RPoint a = p->pts[i], b = p->pts[(i + 1) % p->n];
        if ((a.y > y) == (b.y > y)) continue;
        double t = (y - a.y) / (b.y - a.y);
        if (a.x + (b.x - a.x) * t > x) ++c;
    }
    return c & 1;
}
static void ref_render(const RScene *s, const RBox *box, int w, int h, unsigned char *out) {
    int ss = 4;
    double dx = (box->maxx - box->minx) / w, dy = (box->maxy - box->miny) / h;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int hit = 0;
            for (int j = 0; j < ss; ++j)
                for (int i = 0; i < ss; ++i) {
                    double px = box->minx + (x + (i + 0.5) / ss) * dx;
                    double py = box->miny + (y + (j + 0.5) / ss) * dy;
                    for (int k = 0; k < s->npolys; ++k)
                        if (ref_inside(&s->polys[k], px, py)) { hit = 1; break; }
                }
            out[y * w + x] = hit ? 255 : 0;
        }
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

    /* ---- 掩码解析与校验 ---- */
    unsigned short m = 0;
    CK(raster_mask_parse("1100100110100101", &m) == 0);
    CK(raster_mask_count(m) == 8);
    CK(raster_mask_parse("101010", &m) == -2);          /* 位数不足 */
    CK(raster_mask_parse("10101010101010101", &m) == -2);/* 位数超出 */
    CK(raster_mask_parse("101x101010101010", &m) == -3);/* 非法字符 */
    CK(raster_mask_parse(NULL, &m) == -1);
    CK(raster_mask_parse("0000000000000000", &m) == 0 && raster_mask_count(m) == 0);
    CK(raster_mask_parse("1111111111111111", &m) == 0 && raster_mask_count(m) == 16);

    /* ---- 全零掩码拒绝渲染；全一掩码等价 16 点满网格（灰度）---- */
    unsigned char mb[16 * 16];
    unsigned short zm = 0, om = 0xFFFF;
    CK(raster_fill_mask(&s, &b, 16, 16, zm, mb) == -4);
    CK(raster_fill_mask(&s, &b, 16, 16, om, mb) == 0);

    /* ---- 关闭新逻辑：等分路径与独立参照逐字节一致 ---- */
    unsigned char a_buf[32 * 32], r_buf[32 * 32];
    RPoint tri[3] = {{1, 1}, {30, 6}, {12, 31}};
    RScene s2; scene_init(&s2);
    scene_add_poly(&s2, tri, 3);
    RBox b2 = {0, 0, 32, 32};
    CK(raster_fill(&s2, &b2, 32, 32, 4, a_buf) == 0);
    ref_render(&s2, &b2, 32, 32, r_buf);
    CK(memcmp(a_buf, r_buf, sizeof a_buf) == 0);
    /* 管线（固定等分 ss=4）同样逐字节一致 */
    CK(pipeline_render(&s2, p, b2, 32, 32, a_buf) == 0);
    CK(memcmp(a_buf, r_buf, sizeof a_buf) == 0);

    /* ---- 全一掩码（16 点全用）的二值支持与等分路径严格相同：同一组 16 个 inside 判定 ----
     * 注意：稀疏子集不保证此性质——多边形可能只覆盖被剔除的子格（顶点/细特征处），
     * 故这里只对全一掩码断言；稀疏掩码仅断言输出为合法灰度。 */
    unsigned char g_buf[32 * 32], k_buf[32 * 32];
    CK(raster_fill(&s2, &b2, 32, 32, 4, g_buf) == 0);
    CK(raster_fill_mask(&s2, &b2, 32, 32, 0xFFFF, k_buf) == 0);
    int support_same = 1;
    for (int z = 0; z < 32 * 32; ++z)
        if ((g_buf[z] ? 1 : 0) != (k_buf[z] > 0 ? 1 : 0)) { support_same = 0; break; }
    CK(support_same);
    CK(nonzero_count(k_buf, 32 * 32) == export_count_on(g_buf, 32, 32));
    /* 稀疏掩码：输出为 255*h/K 的合法离散层级，且非空时存在非零像素 */
    const unsigned short probe[] = {0xC9A5, 0xA5C9, 0x6996, 0x9669, 0x0001};
    for (unsigned t = 0; t < sizeof probe / sizeof probe[0]; ++t) {
        int K = raster_mask_count(probe[t]);
        unsigned char level[17];
        for (int lv = 0; lv <= K; ++lv) level[lv] = (unsigned char)(255 * lv / K);
        CK(raster_fill_mask(&s2, &b2, 32, 32, probe[t], k_buf) == 0);
        CK(nonzero_count(k_buf, 32 * 32) > 0);
        int valid_levels = 1;
        for (int z = 0; z < 32 * 32; ++z) {
            int ok = 0;
            for (int lv = 0; lv <= K; ++lv) if (k_buf[z] == level[lv]) { ok = 1; break; }
            if (!ok) { valid_levels = 0; break; }
        }
        CK(valid_levels);
    }

    /* ---- 管线掩码变体：全零拒绝、缺省与掩码路径都可跑 ---- */
    CK(pipeline_render_mask(&s2, p, b2, 32, 32, 0, k_buf) == -4);
    CK(pipeline_render_mask(&s2, p, b2, 32, 32, 0xC9A5, k_buf) == 0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
