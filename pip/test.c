#include "pip.h"
#include <stdio.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

int main(void) {
    /* 逆时针正方形 (0,0)-(10,10) */
    PPoint sq[4] = {{0,0},{10,0},{10,10},{0,10}};
    CK(pip_signed_area(sq,4) > 0.0);
    PPoint cw[4] = {{0,0},{0,10},{10,10},{10,0}};
    CK(pip_signed_area(cw,4) < 0.0);

    /* 9 个手算点：逐点手算绕数与内外 */
    PPoint pts[9] = {{5,5},{15,5},{-5,5},{5,-5},{5,15},{0,5},{10,10},{2,2},{9,9}};
    int exp_w[9]  = {1,0,0,0,0,1,0,1,1};    /* 手算绕数 */
    int exp_in[9] = {1,0,0,0,0,-1,-1,1,1};   /* -1 = 边上，随机判内判外 */
    PIPVerdict v[9];
    int err_at = -99;
    CK(pip_batch(sq,4,pts,9,1,v,&err_at) == 0);
    for (int i = 0; i < 9; ++i) {
        CK(v[i].winding == exp_w[i]);
        if (exp_in[i] >= 0) {
            CK(v[i].on_edge == 0);
            CK(v[i].result == (exp_in[i] ? PIP_INSIDE : PIP_OUTSIDE));
        } else {
            CK(v[i].on_edge == 1);
            CK(v[i].result == PIP_INSIDE || v[i].result == PIP_OUTSIDE);
        }
    }
    /* 边号逐点核对 */
    CK(v[0].n_edges == 1 && v[0].edges[0] == 1);
    CK(v[1].n_edges == 0);
    CK(v[2].n_edges == 2 && v[2].edges[0] == 1 && v[2].edges[1] == 3);
    CK(v[3].n_edges == 0);
    CK(v[7].n_edges == 1 && v[7].edges[0] == 1);
    CK(v[8].n_edges == 1 && v[8].edges[0] == 1);

    /* 顺时针多边形：绕数反号，判定不变 */
    PIPVerdict w;
    CK(pip_point(cw,4,pts[0],&w) == 0 && w.winding == -1 && w.result == PIP_INSIDE);
    CK(pip_point(cw,4,pts[1],&w) == 0 && w.winding == 0 && w.result == PIP_OUTSIDE);

    /* 不启用：直接返回未判定 */
    CK(pip_batch(sq,4,pts,9,0,v,&err_at) == 0);
    for (int i = 0; i < 9; ++i) CK(v[i].result == PIP_UNDECIDED);

    /* 点数为零、顶点不足：报错并指出序号 */
    CK(pip_batch(sq,4,pts,0,1,v,&err_at) == -1 && err_at == 0);
    CK(pip_batch(sq,2,pts,9,1,v,&err_at) == -1 && err_at == 2);
    CK(pip_batch(sq,1,pts,9,1,v,&err_at) == -1 && err_at == 1);

    /* 退化多边形（零面积） */
    PPoint line[3] = {{0,0},{1,1},{2,2}};
    CK(pip_batch(line,3,pts,9,1,v,&err_at) == -2);

    /* 判定表导出：逐点列出边号与绕数 */
    CK(pip_batch(sq,4,pts,9,1,v,&err_at) == 0);
    FILE *fp = tmpfile();
    CK(fp != NULL);
    CK(pip_table_csv(fp,pts,v,9) == 0);
    rewind(fp);
    char buf[256];
    CK(fgets(buf,sizeof buf,fp) != NULL && strstr(buf,"winding") && strstr(buf,"edges"));
    CK(fgets(buf,sizeof buf,fp) != NULL && strstr(buf,"0,5.000000,5.000000,0,1,1,inside"));
    CK(fgets(buf,sizeof buf,fp) != NULL);
    CK(fgets(buf,sizeof buf,fp) != NULL && strstr(buf,"1|3"));
    fclose(fp);

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
