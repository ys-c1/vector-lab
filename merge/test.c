#include "merge.h"
#include <stdio.h>
#include <math.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static int ring_n(const MMerge *m, int r) { return m->rings[r].count; }

int main(void) {
    /* 段1: [0,0]-[16,16] 正方形（CCW）；段2: [8,8]-[24,24] 正方形（CCW）
       手算并集面积 = 256 + 256 - 64 = 448；交点 2 个；外环 8 点 */
    MPoint s1[4] = {{0,0},{16,0},{16,16},{0,16}};
    MPoint s2[4] = {{8,8},{24,8},{24,24},{8,24}};
    MMerge m;
    CK(merge_polygons(s1,4,1, s2,4,2, 1, &m) == MERGE_OK);
    CK(m.n_hits == 2);
    CK(m.n_rings == 1);
    CK(ring_n(&m,0) == 8);
    CK(fabs(merge_union_area(&m) - 448.0) < 1e-6);
    /* 朴素相加（重叠算两次）= 512，并集应明显更小 */
    CK(merge_union_area(&m) < 512.0 - 1e-6);

    /* 并集开关关闭：必须回到原来的两段结果，逐点一致 */
    MMerge mo;
    CK(merge_polygons(s1,4,1, s2,4,2, 0, &mo) == MERGE_OK);
    CK(mo.union_on == 0);
    CK(mo.n_rings == 2);
    CK(mo.rings[0].count == 4 && mo.rings[1].count == 4);
    int same = 1;
    for (int i = 0; i < 4; ++i) {
        if (fabs(mo.rings[0].pts[i].x - s1[i].x) > 1e-12 ||
            fabs(mo.rings[0].pts[i].y - s1[i].y) > 1e-12) same = 0;
        if (fabs(mo.rings[1].pts[i].x - s2[i].x) > 1e-12 ||
            fabs(mo.rings[1].pts[i].y - s2[i].y) > 1e-12) same = 0;
    }
    CK(same);

    /* 绕序冲突：段2 改为顺时针，必须报错且错误信息带两段编号 */
    MPoint s2cw[4] = {{8,8},{8,24},{24,24},{24,8}};
    MMerge mw;
    CK(merge_polygons(s1,4,7, s2cw,4,9, 1, &mw) == MERGE_E_WIND);
    printf("  [wind] %s\n", merge_last_error());
    char buf[128];
    snprintf(buf, sizeof(buf), "%s", merge_last_error());
    CK(strstr(buf, "7") && strstr(buf, "9"));

    /* 切点重合：两正方形仅在一条边/一点相接（段3 [0,20]-[10,30] 贴在段1 的 x=16? ）
       段1 [0,0]-[16,16]，段3 [16,4]-[26,14]：交于竖直线段 x=16, y 4..14（共边退化） */
    MPoint s3[4] = {{16,4},{26,4},{26,14},{16,14}};
    MMerge mt;
    int rc = merge_polygons(s1,4,1, s3,4,3, 1, &mt);
    CK(rc == MERGE_E_TANGENT);
    printf("  [tangent] %s\n", merge_last_error());
    snprintf(buf, sizeof(buf), "%s", merge_last_error());
    CK(strstr(buf, "1") && strstr(buf, "3"));

    /* 单点相切：段4 仅在 (16,16) 与段1 相接 */
    MPoint s4[4] = {{16,16},{26,16},{26,26},{16,26}};
    MMerge mp;
    CK(merge_polygons(s1,4,1, s4,4,4, 1, &mp) == MERGE_E_TANGENT);
    printf("  [point-touch] %s\n", merge_last_error());

    /* 相离：无交点，各自成环，面积和 = 两段面积和 */
    MPoint far[4] = {{100,100},{110,100},{110,110},{100,110}};
    MMerge md;
    CK(merge_polygons(s1,4,1, far,4,5, 1, &md) == MERGE_OK);
    CK(md.n_hits == 0 && md.n_rings == 2);
    CK(fabs(merge_union_area(&md) - (256.0 + 100.0)) < 1e-6);

    /* 包含：无交点，两段各自成环（并集面积按外环 256 解释，见 README 说明） */
    MPoint inner[4] = {{2,2},{6,2},{6,6},{2,6}};
    MMerge mc;
    CK(merge_polygons(s1,4,1, inner,4,6, 1, &mc) == MERGE_OK);
    CK(mc.n_rings == 2);

    /* 参数校验 */
    CK(merge_polygons(0,4,1, s2,4,2, 1, &m) == MERGE_E_ARG);
    CK(merge_polygons(s1,2,1, s2,4,2, 1, &m) == MERGE_E_ARG);

    /* 多次链式拼接：三个部分重叠的方块，逐段并集仍为单环且面积与容斥一致。
       s8 [14,-4]-[30,12] 偏移后交点全为正常穿越，无顶点相切：
       s1∩s2=64；s8∩s1=2*12=24；s8∩s2=10*4=40；三者公共=2*4=8。
       总并集 = 768 - 64 - 24 - 40 + 8 = 648 */
    MPoint s8[4] = {{14,-4},{30,-4},{30,12},{14,12}};
    MMerge c12;
    CK(merge_polygons(s1,4,1, s2,4,2, 1, &c12) == MERGE_OK);
    MPoint chain[MERGE_RING_CAP];
    int cn = c12.rings[0].count;
    for (int i = 0; i < cn; ++i) chain[i] = c12.rings[0].pts[i];
    MMerge c128;
    CK(merge_polygons(chain, cn, 12, s8,4,8, 1, &c128) == MERGE_OK);
    CK(c128.n_rings == 1);
    CK(fabs(merge_union_area(&c128) - 648.0) < 1e-6);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
