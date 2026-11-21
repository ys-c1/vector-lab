#include "clip.h"
#include "shapes.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
static int fails = 0, checks = 0;
#define CK(c) do { ++checks; if (!(c)) { ++fails; printf("FAIL %d %s\n", __LINE__, #c); } } while (0)

static double poly_abs_area(const CPoly *p) {
    double a = clip_area(p);
    return a < 0 ? -a : a;
}

int main(void) {
    /* ---- 原有凸裁剪行为保持不变 ---- */
    CPoint poly[4] = {{0,0},{10,0},{10,10},{0,10}};
    CPoint win[4] = {{2,2},{8,2},{8,8},{2,8}};
    CPoly out;
    CK(clip_convex(poly,4,win,4,&out) == 0);
    CK(out.count == 4);
    CK(fabs(fabs(clip_area(&out)) - 36.0) < 1e-6);
    CPoint outside[4] = {{-5,-5},{-1,-5},{-1,-1},{-5,-1}};
    CK(clip_convex(outside,4,win,4,&out) == 0 && out.count == 0);
    CK(clip_convex(poly,2,win,4,&out) == -1);

    /* ---- 拆块关闭时与现状逐顶点相同 ---- */
    CPoint uwin[8], cover[4];
    int wn = shape_u_window(uwin);
    int cn = shape_cover(cover);
    CPoly legacy;
    CPoly offs[CLIP_MAX_PIECES];
    int nouts = 0, npieces = 0, ep = -99;
    double ua = 0;
    CK(clip_convex(cover, cn, uwin, wn, &legacy) == 0);
    CK(clip_concave(cover, cn, uwin, wn, 0, offs, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(nouts == (legacy.count > 0 ? 1 : 0));
    if (legacy.count > 0) {
        CK(memcmp(&legacy, &offs[0], sizeof(CPoly)) == 0);
    } else {
        CK(offs[0].count == 0);
    }

    /* ---- 凹窗口拆分裁剪：U 形，全覆盖，手算面积 72 ---- */
    CPoly pc[CLIP_MAX_PIECES];
    nouts = 0; npieces = 0; ua = 0; ep = -99;
    CK(clip_concave(cover, cn, uwin, wn, 1, pc, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(npieces == 3);                 /* 左竖条/右竖条/底横条 */
    CK(nouts == 3);
    CK(fabs(ua - 72.0) < 1e-7);

    /* ---- 拆分本身：块并集面积 = 窗口面积（分割性质） ---- */
    CPoly pieces[CLIP_MAX_PIECES];
    int np = clip_decompose(uwin, wn, pieces, CLIP_MAX_PIECES, &ep);
    CK(np == 3);
    double sum = 0;
    /* 手算三块：左右竖条各 3*8.5=25.5，底横条 10*3-2*4.5=21，合计 72 */
    double expect_piece[3] = {21, 25.5, 25.5};
    double got[3];
    for (int k = 0; k < np; ++k) {
        CK(pieces[k].count >= 3);
        double a = poly_abs_area(&pieces[k]);
        got[k] = a;
        sum += a;
    }
    CK(fabs(sum - 72.0) < 1e-7);
    for (int i = 0; i < 3; ++i)
        for (int j = i + 1; j < 3; ++j)
            if (got[j] < got[i]) { double t = got[i]; got[i] = got[j]; got[j] = t; }
    for (int i = 0; i < 3; ++i) CK(fabs(got[i] - expect_piece[i]) < 1e-7);

    /* ---- 部分覆盖：左半，手算并集 36 ---- */
    CPoint half[4];
    int hn = shape_left_half(half);
    nouts = 0; ua = 0; npieces = 0; ep = -99;
    CK(clip_concave(half, hn, uwin, wn, 1, pc, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(fabs(ua - 36.0) < 1e-7);
    CK(nouts == 2); /* 左竖条完整 + 底横条被切一半 */

    /* ---- 被裁多边形完全在窗口内但跨过块间内部边：切成多块，面积不丢 ---- */
    CPoint inside[4] = {{1,0.5},{2,0.5},{2,1.5},{1,1.5}};
    nouts = 0; ua = 0; npieces = 0; ep = -99;
    CK(clip_concave(inside, 4, uwin, wn, 1, pc, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(nouts == 2); /* 内部对角线 y=x 把它分进左竖条与底横条 */
    CK(fabs(ua - 1.0) < 1e-9);

    /* 完全落在单个凸块内部的小矩形：1 块、面积原样 */
    CPoint inside2[4] = {{4,0.5},{6,0.5},{6,1.5},{4,1.5}};
    nouts = 0; ua = 0; npieces = 0; ep = -99;
    CK(clip_concave(inside2, 4, uwin, wn, 1, pc, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(nouts == 1);
    CK(fabs(ua - 2.0) < 1e-9);

    /* ---- 被裁多边形与窗口完全不相交：0 块、0 面积，不算错误 ---- */
    CPoint away[4] = {{50,50},{60,50},{60,60},{50,60}};
    nouts = 0; ua = -1; npieces = 0; ep = -99;
    CK(clip_concave(away, 4, uwin, wn, 1, pc, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(nouts == 0);
    CK(ua == 0.0);
    CK(npieces == 3); /* 窗口拆分照常完成，只是各块裁剪结果为空 */

    /* ---- 64 顶点梳齿窗口：手算面积 55 ---- */
    CPoint cwin[CLIP_MAX_VERTS];
    int cwn = shape_comb_window(cwin);
    CK(cwn == 64);
    CPoly cpieces[CLIP_MAX_PIECES];
    int cnp = clip_decompose(cwin, cwn, cpieces, CLIP_MAX_PIECES, &ep);
    CK(cnp >= 1);
    double csum = 0;
    for (int k = 0; k < cnp; ++k) {
        CK(cpieces[k].count >= 3);
        csum += poly_abs_area(&cpieces[k]);
    }
    CK(fabs(csum - 55.0) < 1e-6);
    nouts = 0; ua = 0; npieces = 0; ep = -99;
    CK(clip_concave(cover, cn, cwin, cwn, 1, pc, CLIP_MAX_PIECES,
                    &nouts, &ua, &npieces, &ep) == 0);
    CK(fabs(ua - 55.0) < 1e-6);

    /* ---- 凸窗口拆成 1 块 ---- */
    CPoly sqp[CLIP_MAX_PIECES];
    CK(clip_decompose(win, 4, sqp, CLIP_MAX_PIECES, &ep) == 1);
    CK(fabs(poly_abs_area(&sqp[0]) - 36.0) < 1e-7);

    /* ---- 错误：凸块顶点不足（共线退化三角形），块号为 #0 ---- */
    CPoint flat[3] = {{0,0},{5,0},{10,0}};
    ep = -99;
    int rc = clip_decompose(flat, 3, pieces, CLIP_MAX_PIECES, &ep);
    CK(rc == CLIP_E_PIECE_VERTS);
    CK(ep == 0);
    char msg[160];
    clip_strerror(rc, ep, msg, sizeof msg);
    CK(strstr(msg, "#0") != NULL);

    /* ---- 错误：自交多边形在 #1 块退化，报顶点不足并指出块号 ---- */
    CPoint bow[5] = {{0,0},{10,10},{10,0},{0,10},{0,0}};
    ep = -99;
    rc = clip_decompose(bow, 5, pieces, CLIP_MAX_PIECES, &ep);
    CK(rc == CLIP_E_PIECE_VERTS);
    CK(ep == 1);
    clip_strerror(rc, ep, msg, sizeof msg);
    CK(strstr(msg, "#1") != NULL);

    /* ---- 错误：块数为零时的报错必须指出块号 ---- */
    clip_strerror(CLIP_E_NO_PIECES, 0, msg, sizeof msg);
    CK(strstr(msg, "0 pieces") != NULL);
    CK(strstr(msg, "#0") != NULL);

    /* ---- 错误：参数 ---- */
    CK(clip_decompose(NULL, 4, pieces, CLIP_MAX_PIECES, &ep) == CLIP_E_ARG);
    CK(clip_decompose(win, 2, pieces, CLIP_MAX_PIECES, &ep) == CLIP_E_ARG);
    CK(clip_concave(cover, cn, uwin, wn, 1, pc, 0,
                    &nouts, &ua, &npieces, &ep) == CLIP_E_ARG);

    /* ---- 块表 CSV 导出 ---- */
    FILE *fp = fopen("build/pieces_u8.csv", "w");
    CK(fp != NULL);
    long wb = clip_pieces_csv(pieces, np, fp);
    fclose(fp);
    CK(wb > 0);
    fp = fopen("build/pieces_u8.csv", "r");
    CK(fp != NULL);
    char line[256];
    CK(fgets(line, sizeof line, fp) != NULL);
    CK(strncmp(line, "piece,vertex,x,y,piece_area", 27) == 0);
    int data_rows = 0;
    while (fgets(line, sizeof line, fp)) {
        CK(line[0] >= '0' && line[0] <= '9');
        ++data_rows;
    }
    fclose(fp);
    CK(data_rows == 12); /* U 窗口 3 块：4+4+4 共 12 个顶点行 */

    printf("%d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
