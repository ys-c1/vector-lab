#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/jitter.h"
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

    /* ---- jitter: rejection rules ---- */
    RJitter seq[16];
    CK(jitter_gen(0, 4, seq) == -1);          /* zero seed rejected */
    CK(jitter_gen(7, 0, seq) == -1);          /* zero length rejected */
    CK(jitter_gen(7, -1, seq) == -1);
    CK(jitter_gen(7, 4, NULL) == -1);
    CK(jitter_export("/tmp/should_not_exist_dir/x.txt", 7, 4) == -2);

    /* ---- jitter: determinism, two independent draws ---- */
    RJitter a[64], c[64];
    CK(jitter_gen(123456789ULL, 64, a) == 0);
    CK(jitter_gen(123456789ULL, 64, c) == 0);
    CK(memcmp(a, c, sizeof a) == 0);
    RJitter d[64];
    CK(jitter_gen(987654321ULL, 64, d) == 0);
    CK(memcmp(a, d, sizeof a) != 0);
    for (int i = 0; i < 64; ++i) {
        CK(a[i].ux >= 0.0 && a[i].ux < 1.0);
        CK(a[i].uy >= 0.0 && a[i].uy < 1.0);
    }

    /* ---- known-answer vector for seed 1, length 4 (baked, see README) ---- */
    RJitter kat[4];
    CK(jitter_gen(1ULL, 4, kat) == 0);
    static const double expect[4][2] = {
        {0.5665615751722809, 0.74578175726270113},
        {0.97100275358679622, 0.44435921705577208},
        {0.44426470082635805, 0.76289439191176101},
        {0.87734868676417299, 0.52306717985098139},
    };
    for (int i = 0; i < 4; ++i) {
        CK(kat[i].ux == expect[i][0]);
        CK(kat[i].uy == expect[i][1]);
    }

    /* ---- jitter raster: byte-identical across repeated runs.
     * Use a rotated square so slanted edges actually cross subcells. ---- */
    RPlace tilt = {0, 0, 30.0 * 3.14159265358979323846 / 180.0, 1.0};
    RScene moved;
    scene_init(&moved);
    RPoly rotp = s.polys[0];
    double cc = 0.8660254037844386, sn = 0.5;
    for (int i = 0; i < rotp.n; ++i) {
        RPoint v = rotp.pts[i];
        rotp.pts[i].x = v.x * cc - v.y * sn;
        rotp.pts[i].y = v.x * sn + v.y * cc;
    }
    CK(scene_add_poly(&moved, rotp.pts, rotp.n) == 0);
    unsigned char r1[32 * 32], r2[32 * 32];
    CK(raster_fill_jitter(&moved, &b, 32, 32, 4, 42ULL, r1) == 0);
    CK(raster_fill_jitter(&moved, &b, 32, 32, 4, 42ULL, r2) == 0);
    CK(memcmp(r1, r2, sizeof r1) == 0);
    unsigned char r3[32 * 32];
    CK(raster_fill_jitter(&moved, &b, 32, 32, 4, 43ULL, r3) == 0);
    CK(memcmp(r1, r3, sizeof r1) != 0);      /* different seed -> different image */
    CK(raster_fill_jitter(&moved, &b, 32, 32, 4, 0ULL, r1) == -5);
    CK(raster_fill_jitter(NULL, &b, 32, 32, 4, 1ULL, r1) == -1);

    /* grayscale coverage appears along the slanted edge */
    int shades = 0;
    for (int i = 0; i < 32 * 32; ++i)
        if (r1[i] != 0 && r1[i] != 255) ++shades;
    CK(shades > 0);

    /* ---- pipeline jitter path ---- */
    CK(pipeline_render_jitter(&s, tilt, b, 16, 16, 4, 99ULL, buf) == 0);
    CK(pipeline_render_jitter(&s, tilt, b, 16, 16, 4, 0ULL, buf) == -5);
    CK(pipeline_render_jitter(&s, zero, b, 16, 16, 4, 99ULL, buf) == -2);
    CK(pipeline_render_jitter(&s, tilt, bad, 16, 16, 4, 99ULL, buf) == -3);

    printf("KAT seed1: %.17g %.17g | %.17g %.17g | %.17g %.17g | %.17g %.17g\n",
           kat[0].ux, kat[0].uy, kat[1].ux, kat[1].uy,
           kat[2].ux, kat[2].uy, kat[3].ux, kat[3].uy);
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
