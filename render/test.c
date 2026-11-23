#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/backend.h"
#include "src/tile.h"
#include "src/export.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

/* 确定性 LCG，保证两后端在完全相同的随机场景上差分。 */
static unsigned long rng = 0x12345678UL;
static double frand(double lo, double hi) {
    rng = rng * 1103515245UL + 12345UL;
    double u = (double)(rng >> 8) / (double)(1UL << 24);
    return lo + u * (hi - lo);
}

static int diff_bytes(const unsigned char *a, const unsigned char *b, size_t n) {
    size_t d = 0;
    for (size_t i = 0; i < n; ++i) d += (a[i] != b[i]);
    return (int)d;
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

    /* ---- 后端选择器：0 与越界必须停机，且不触碰 buf ---- */
    CK(raster_backend_valid(RASTER_BACKEND_SCANLINE) == 0);
    CK(raster_backend_valid(RASTER_BACKEND_TILE) == 0);
    CK(raster_backend_valid(0) != 0);
    CK(raster_backend_valid(3) != 0);
    CK(raster_backend_valid(-1) != 0);
    unsigned char guard[64];
    memset(guard, 0xAA, sizeof guard);
    CK(raster_render(0, &s, &b, 8, 8, 4, guard) == -2);
    CK(raster_render(3, &s, &b, 8, 8, 4, guard) == -2);
    CK(raster_render(-1, &s, &b, 8, 8, 4, guard) == -2);
    for (size_t i = 0; i < sizeof guard; ++i) CK(guard[i] == 0xAA);
    CK(pipeline_render_backend(0, &s, p, b, 8, 8, guard) == -2);
    CK(pipeline_render_backend(9, &s, p, b, 8, 8, guard) == -2);
    CK(raster_render(1, NULL, &b, 8, 8, 4, guard) == -1);
    CK(raster_render(2, &s, &b, 0, 8, 4, guard) == -1);
    CK(raster_render(2, &s, &b, 8, 8, 9, guard) == -1);

    /* ---- 固定场景差分：含重叠、洞套、越出视口、水平/垂直边 ---- */
    struct { int w, h, ss; } dims[] = {
        {1,1,4}, {7,5,1}, {31,33,2}, {32,32,4}, {33,17,8},
        {64,64,4}, {100,70,3}, {129,65,2}, {200,200,4}
    };
    RScene fx;
    scene_init(&fx);
    RPoint a[4] = {{0,0},{10,0},{10,10},{0,10}};
    RPoint c[4] = {{3,3},{7,3},{7,7},{3,7}};      /* 内框（奇偶镂空） */
    RPoint t[3] = {{-5,-5},{2,-5},{2,2}};          /* 越出视口 */
    RPoint r[5] = {{12,1},{18,2},{17,9},{11,11},{9,6}};
    RPoint hz[4] = {{1,9},{9,9},{9,9.5},{1,9.5}};  /* 水平长条 */
    CK(scene_add_poly(&fx, a, 4) == 0);
    CK(scene_add_poly(&fx, c, 4) == 0);
    CK(scene_add_poly(&fx, t, 3) == 0);
    CK(scene_add_poly(&fx, r, 5) == 0);
    CK(scene_add_poly(&fx, hz, 4) == 0);
    RBox fb = {-2, -2, 12, 12};
    for (size_t d = 0; d < sizeof dims / sizeof dims[0]; ++d) {
        size_t n = (size_t)dims[d].w * dims[d].h;
        unsigned char *b1 = (unsigned char*)malloc(n), *b2 = (unsigned char*)malloc(n);
        CK(b1 && b2);
        CK(raster_render(1, &fx, &fb, dims[d].w, dims[d].h, dims[d].ss, b1) == 0);
        CK(raster_render(2, &fx, &fb, dims[d].w, dims[d].h, dims[d].ss, b2) == 0);
        int df = diff_bytes(b1, b2, n);
        if (df) printf("FAIL fixed scene dims[%zu] diff=%d\n", d, df);
        CK(df == 0);
        free(b1); free(b2);
    }

    /* ---- 随机场景差分：多边形数、顶点数、画幅、ss 全随机 ---- */
    int trials = 300;
    for (int tr = 0; tr < trials; ++tr) {
        RScene rs;
        scene_init(&rs);
        int npoly = 1 + (int)(rng % 6);
        for (int k = 0; k < npoly; ++k) {
            RPoint pts[64];
            int n = 3 + (int)(rng % 10);
            double cx = frand(-20, 120), cy = frand(-20, 120);
            double rad = frand(2, 25);
            for (int i = 0; i < n; ++i) {
                pts[i].x = cx + frand(-rad, rad);
                pts[i].y = cy + frand(-rad, rad);
            }
            if (scene_add_poly(&rs, pts, n) != 0) { CK(0); break; }
        }
        RBox rb;
        CK(scene_bbox(&rs, &rb) == 0);
        /* 视口有时收紧、有时外扩，制造瓦片剔除边界。 */
        double pad = frand(-3, 8);
        rb.minx -= pad; rb.miny -= pad; rb.maxx += pad; rb.maxy += pad;
        if (!(rb.maxx > rb.minx) || !(rb.maxy > rb.miny)) continue;
        int w = 1 + (int)(rng % 140), h = 1 + (int)(rng % 140);
        int ss = 1 + (int)(rng % 8);
        size_t n = (size_t)w * h;
        unsigned char *b1 = (unsigned char*)malloc(n), *b2 = (unsigned char*)malloc(n);
        CK(b1 && b2);
        CK(raster_render(1, &rs, &rb, w, h, ss, b1) == 0);
        CK(raster_render(2, &rs, &rb, w, h, ss, b2) == 0);
        int df = diff_bytes(b1, b2, n);
        if (df) { printf("FAIL trial %d w=%d h=%d ss=%d diff=%d\n", tr, w, h, ss, df); CK(0); }
        free(b1); free(b2);
    }

    /* ---- pipeline 层两后端一致（含旋转/缩放放置） ---- */
    for (int rot = 0; rot < 4; ++rot) {
        RPlace pl = {frand(-3, 3), frand(-3, 3), 0.3 * rot, frand(0.5, 2.0)};
        size_t n = 97 * 61;
        unsigned char *b1 = (unsigned char*)malloc(n), *b2 = (unsigned char*)malloc(n);
        CK(b1 && b2);
        CK(pipeline_render_backend(1, &fx, pl, fb, 97, 61, b1) == 0);
        CK(pipeline_render_backend(2, &fx, pl, fb, 97, 61, b2) == 0);
        CK(diff_bytes(b1, b2, n) == 0);
        free(b1); free(b2);
    }

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
