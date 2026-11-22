#include "src/scene.h"
#include "src/pipeline.h"
#include "src/raster.h"
#include "src/export.h"
#include "src/interval.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static void interval_tests(void) {
    /* 单值区间 */
    Ival z = iv_s(3.0);
    CK(z.lo == 3.0 && z.hi == 3.0 && iv_width(&z) == 0.0);
    /* 加法向外扩：结果区间必须包住两端各一次最近舍入的结果 */
    Ival a = iv_s(1.0), b = iv_s(10.0), r;
    CK(iv_add(&a, &b, &r) == IVL_OK);
    CK(r.lo <= 11.0 && r.hi >= 11.0 && iv_width(&r) > 0.0);
    /* 区间运算包围性：取区间内两个点，其乘积必在结果区间内 */
    Ival x = {0.999, 1.001}, y = {2.0, 2.0};
    CK(iv_mul(&x, &y, &r) == IVL_OK);
    CK(r.lo <= 0.999 * 2.0 && r.hi >= 1.001 * 2.0);
    /* 除零区间必须被拒绝 */
    Ival dz = {-1.0, 1.0}, one = iv_s(1.0);
    CK(iv_div(&one, &dz, &r) == IVL_DIVZERO);
    /* 三角区间包住 libm 直算值 */
    Ival ang = iv_s(0.7), cs, sn;
    CK(iv_cos(&ang, &cs) == IVL_OK && iv_sin(&ang, &sn) == IVL_OK);
    CK(cs.lo <= cos(0.7) && cs.hi >= cos(0.7));
    CK(sn.lo <= sin(0.7) && sn.hi >= sin(0.7));
    /* 下溢截断：严格为正的最次正规区间乘 0.25，精确值已不可表达 */
    double tiny = nextafter(4.9406564584124654e-324, INFINITY);
    Ival t = {tiny, nextafter(tiny, INFINITY)}, q = iv_s(0.25);
    CK(iv_mul(&t, &q, &r) == IVL_TRUNC);
    /* 精确的 0 乘不截断 */
    Ival zero = iv_s(0.0);
    CK(iv_mul(&zero, &q, &r) == IVL_OK);
    /* 非有限端点上报 */
    Ival inf = {INFINITY, INFINITY};
    CK(iv_add(&inf, &one, &r) == IVL_NONFINITE);
}

int main(void) {
    interval_tests();
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
    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
