#include "hull.h"
#include <stdio.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    HPoint p[5] = {{0,0},{4,0},{4,3},{0,3},{2,1}};
    HPoly out;
    CK(hull_convex(p, 5, &out) == 0);
    CK(out.count == 4);
    double cx, cy, w, h, a;
    CK(hull_min_rect(p, 5, &cx, &cy, &w, &h, &a) == 0);
    CK(w > 3.9 && w < 4.1 && h > 2.9 && h < 3.1);
    CK(hull_convex(p, 0, &out) == -1);
    CK(hull_convex(p, 1, &out) == 0 && out.count == 1);

    /* 洋葱剥层：四层严格分离的同心正方形（半径 8/6/4/2）+ 中心点。 */
    HPoint sq[17];
    int n = 0;
    double rad[4] = {8.0, 6.0, 4.0, 2.0};
    for (int layer = 0; layer < 4; ++layer) {
        double d = rad[layer];
        sq[n++] = (HPoint){-d, -d};
        sq[n++] = (HPoint){ d, -d};
        sq[n++] = (HPoint){ d,  d};
        sq[n++] = (HPoint){-d,  d};
    }
    sq[n++] = (HPoint){0, 0};
    LResult res;
    CK(hull_layers(sq, n, &res) == 0);
    CK(res.nlayers == 4);
    CK(res.nrem == 1);                          /* 中心点不足三点 */
    CK(res.layers[0].count == 4);
    double a0 = hull_poly_area(&res.layers[0]);
    double a3 = hull_poly_area(&res.layers[3]);
    CK(a0 > 255.9 && a0 < 256.1);              /* (2*8)^2 = 256 */
    CK(a3 > 15.9 && a3 < 16.1);                /* (2*2)^2 = 16 */

    /* 反解层号：第 k 层面积比 = (rad[k]/rad[0])^2，k=2 时 1/4=0.25。 */
    int k = hull_solve_layer(&res, 0.5);
    CK(k == 2);                                 /* 比值序列 1, .5625, .25, .0625 */
    CK(hull_solve_layer(&res, 0.0) == -1);
    CK(hull_solve_layer(&res, 1.0) == -1);
    int kmiss = hull_solve_layer(&res, 0.001); /* 最小层比值 0.0625，达不到 */
    CK(kmiss == -3);
    /* 超界层号必须被渲染接口拒绝。 */
    unsigned char buf[64];
    CK(hull_layers_pgm(&res, res.nlayers, 8, 8, 0.02, buf) == -1);
    CK(hull_layers_pgm(&res, -1, 8, 8, 0.02, buf) == -1);
    CK(hull_layers_pgm(&res, 1, 8, 8, 0.02, buf) == 0);
    int nz = 0;
    for (int i = 0; i < 64; ++i) if (buf[i]) ++nz;
    CK(nz > 0);

    /* 全共线点集：一层都构不成，剩余全部点且 <3 成层条件由 nlayers=0 表达。 */
    HPoint line[8];
    for (int i = 0; i < 8; ++i) line[i] = (HPoint){(double)i, 0.5 * i};
    LResult lr;
    CK(hull_layers(line, 8, &lr) == 0);
    CK(lr.nlayers == 0);
    CK(lr.nrem == 8);

    /* 双簇：两层以上，剩余点数少于 3 时给出残集。 */
    HPoint cl[8] = {{0,0},{10,0},{10,10},{0,10},{4,4},{6,4},{6,6},{4,6}};
    LResult cr;
    CK(hull_layers(cl, 8, &cr) == 0);
    CK(cr.nlayers == 2);
    CK(cr.nrem == 0);

    /* 层数上限：嵌套正方形数量超过 HULL_MAX_LAYERS 时必须拒绝并返回 -2。
     * 每层严格分离（半径递减 2），共 HULL_MAX_LAYERS+1 层。 */
    enum { N_CAP = HULL_MAX_LAYERS + 1 };
    HPoint many[N_CAP * 4];
    int mn = 0;
    for (int t = 0; t < N_CAP; ++t) {
        double r = 2.0 * (N_CAP - t);
        many[mn++] = (HPoint){-r, -r};
        many[mn++] = (HPoint){ r, -r};
        many[mn++] = (HPoint){ r,  r};
        many[mn++] = (HPoint){-r,  r};
    }
    LResult mr;
    CK(hull_layers(many, mn, &mr) == -2);
    CK(hull_layers(NULL, 1, &mr) == -1);
    CK(hull_poly_area(NULL) == 0.0);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
